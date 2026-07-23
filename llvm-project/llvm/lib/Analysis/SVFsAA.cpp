//===- SVFsAA.cpp - Minimal SVFs AA implementation --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/SVFsAA.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include <queue>
#include <fstream>
#include <regex>
#include <string>
#include <chrono>
#include <iostream>
#include <mysql/mysql.h>
#include <libpq-fe.h>
#include <omp.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <fmt/core.h>
#include "llvm/ADT/Statistic.h"
#include <charconv>
#define DEBUG_TYPE "svfs-aa"
// #define DEBUG_PRINT
// #define DEBUG_PRINT_INTERPROCEDURAL
// #define DEBUG_PRINT_Instructions_alias
// #define LOG_compile_time
using namespace llvm;

STATISTIC(NumSVFsAAQueries, "Number of SVFsAA queries");
STATISTIC(NumSVFsAARepeatedQueries, "Number of SVFsAA queries that were repeated (cache hits)");
STATISTIC(NumSVFsAALL, "Number of SVFsAA queries that were load-load");
// STATISTIC(NumSVFsAAResults, "Number of SVFsAA results");
STATISTIC(NumSVFsAAMayAlias, "Number of SVFsAA queries that returned MayAlias");
STATISTIC(NumSVFsAANoAlias, "Number of SVFsAA queries that returned NoAlias");
STATISTIC(NumSVFsAAUnknown, "Number of SVFsAA queries that returned MayAlias due to unknown result");
STATISTIC(NumSVFsAANotFound, "Number of Entries that are not answered by SVFsAA and default to MayAlias");
bool SVFsAAResult::invalidate(Function &Fn, const PreservedAnalyses &PA,
                               FunctionAnalysisManager::Invalidator &Inv) {
  // Check whether the analysis has been explicitly invalidated. Otherwise, it's
  // stateless and remains preserved.
  auto PAC = PA.getChecker<SVFsAA>();
  return !PAC.preservedWhenStateless();
}

uint64_t GetMemId_Inst(const Instruction *inst) {
    // Generate a unique memory ID based on the instruction type and operands
if (isa<LoadInst>(inst) || isa<StoreInst>(inst)) {
      // Try to get the metadata node attached to this instruction
      if (MDNode *Node = inst->getMetadata("mem_id")) {
        // Check that the node has at least 2 operands
        if (Node->getNumOperands() >= 2) {
          Metadata *Op1 = Node->getOperand(1).get();

          // Expecting a ConstantInt wrapped in ConstantAsMetadata
          if (auto *ConstMD = dyn_cast<ConstantAsMetadata>(Op1)) {
            if (auto *CI = dyn_cast<ConstantInt>(ConstMD->getValue())) {
              uint64_t MemID = CI->getZExtValue();
              // errs() << "Found mem_id = " << MemID << " on instruction: " << *inst << "\n";
              return MemID; // Return the memory ID
            }
          }
        }
      }
    }
  // errs() << "No mem_id found for instruction: " << *inst << "\n";
  return -1; // Default return value if no mem_id found
}

uint64_t GetMemId_Spec_calls(const Instruction *inst) {
    if (auto *Call = dyn_cast<CallInst>(inst)) {
      #ifdef DEBUG_PRINT_INTERPROCEDURAL
      errs() << "CallInst found: " << *Call << "\n";
      #endif
        if (MDNode *Node = Call->getMetadata("call_id")) {
          #ifdef DEBUG_PRINT_INTERPROCEDURAL
          errs() << "Found call_id metadata on instruction: " << *Call << "\n";
          #endif
            if (Node->getNumOperands() >= 2) {
              #ifdef DEBUG_PRINT_INTERPROCEDURAL
              errs() << "Metadata has sufficient operands\n";
              #endif
                if (auto *ConstMD = dyn_cast<ConstantAsMetadata>(Node->getOperand(1).get())) {
                  #ifdef DEBUG_PRINT_INTERPROCEDURAL
                  errs() << "Found call_id constant metadata: " << *ConstMD << "\n";
                  #endif
                    if (auto *CI = dyn_cast<ConstantInt>(ConstMD->getValue())) {
                        #ifdef DEBUG_PRINT_INTERPROCEDURAL
                        errs() << "Found call_id constant: " << *CI << "\n";
                        #endif
                        uint64_t CallId = CI->getZExtValue();
                        #ifdef DEBUG_PRINT_INTERPROCEDURAL
                        errs() << "Found call_id = " << CallId << " on instruction: " << *Call << "\n";
                        #endif
                        return CallId;
                    }
                }
            }
        }
    }
    return UINT64_MAX; // sentinel
}

std::string parallel_query_tables(MYSQL* base_conn, const std::string& baseName, const std::string& key, MYSQL_ROW row) {
    int nTables = atoi(row[0]);
    std::string aaValue = "";
    bool found = false;

    omp_set_num_threads(8);

    #pragma omp parallel for shared(found)
    for (int i = 0; i < nTables; i++) {
        if (found) continue;  // Early skip if already found

        // Each thread needs its own connection
        MYSQL* conn = mysql_init(NULL);
        if (!mysql_real_connect(conn, "localhost", "root", "password", "Test", 0, NULL, 0)) {
            std::cerr << "Thread " << omp_get_thread_num()
                      << " failed to connect: " << mysql_error(conn) << "\n";
            mysql_close(conn);
            continue;
        }

        std::string tableName = baseName;
        tableName += "_" + std::to_string(i+1);

        std::string query = "SELECT aa FROM " + tableName +
                            " WHERE ptr_ptr_func = '" + key + "'";

        #ifdef DEBUG_PRINT
        std::cerr << "Executing query: " << query << "\n";
        #endif

        if (mysql_query(conn, query.c_str())) {
          #ifdef DEBUG_PRINT
            std::cerr << "SELECT failed: " << mysql_error(conn)
                      << " for table: " << tableName << "\n";
          #endif
            mysql_close(conn);
            continue;
        }

        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) {
            std::cerr << "mysql_store_result failed: " << mysql_error(conn)
                      << " for table: " << tableName << "\n";
            mysql_close(conn);
            continue;
        }

        MYSQL_ROW row_aa = mysql_fetch_row(res);
        if (row_aa && row_aa[0]) {
            {
                if (!found) {
                    aaValue = row_aa[0];
                    #ifdef DEBUG_PRINT
                    errs() << "Found record in " << tableName << "Query " << query << " : " << aaValue << "\n";
                    #endif
                    // std::cerr << "Found record in " << tableName << ": " << aaValue << "\n";
                    found = true;
                }
            }
        } else {
            #ifdef DEBUG_PRINT
            errs() << "No matching record in table: " << tableName <<  " Query: " << query << "\n";
            #endif
            // std::cerr << "No matching record in table: " << tableName << "\n";
        }

        mysql_free_result(res);
        mysql_close(conn);
    }

    if (!found) {
      #ifdef DEBUG_PRINT
        errs() << "No result found in any table" << " key: " << key << ".\n";
      #endif
    } else {
      #ifdef DEBUG_PRINT
        errs() << "Final aaValue = " << aaValue << "\n";
      #endif
        // std::cerr << "Final aaValue = " << aaValue << "\n";
    }
    return aaValue;
}

std::string fetchAAByPrimaryKey_Spec_multiple(const std::string &ptr1, const std::string &ptr2, const std::string &moduleName = "", const std::string &functionName = "") {
  // return "MayAlias";
  // for a single module with entries above 2M, it is broken down into multiple tables with suffix _1, _2, etc, reading to database should be parallel
  std::string key; // Assuming this is the primary key column name
  std::string key1; // Assuming this is the primary key column name
  if(moduleName == "" || functionName == "" || ptr1 == "" || ptr2 == "") {
    return "MayAlias"; // Default to MayAlias if module or function name is empty
  }  
  MYSQL* conn = mysql_init(nullptr);
   key = "&" + ptr1 + "@" + "Load" + "&" + ptr2 + "@" + "Store"; // Construct key from inputs
   key1 = "&" + ptr2 + "@" + "Load" + "&" + ptr1 + "@" + "Store"; // Construct key from inputs
#ifdef DEBUG_PRINT
   errs() << "Querying alias for: " << key << "\n";
   errs() << "Querying alias for: " << key1 << "\n";
#endif
    if (conn == nullptr) {
        std::cerr << "mysql_init() failed\n";
        return "MayAlias"; // Default to MayAlias if connection fails
    }

    if (!mysql_real_connect(conn, "localhost", "root", "password", "Test", 0, nullptr, 0)) {
        std::cerr << "mysql_real_connect() failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "MayAlias"; // Default to MayAlias if connection fails
    }
        // Remove file extension (.ll, .bc, etc.)
    size_t lastDot = moduleName.find_last_of(".");
    std::string baseName = moduleName;
    if (lastDot != std::string::npos) {
        baseName = moduleName.substr(0, lastDot);
    }
    std::string innermost_name = baseName.substr(baseName.find_last_of("/\\") + 1);
    baseName = innermost_name;
    // std::string suffix = "_reorderedloads";
    std::regex suffix("_annotated$");
    std::regex ihen("-");
    baseName = std::regex_replace(baseName, suffix, "");
    baseName = std::regex_replace(baseName, ihen, "__");
    baseName += "_annotated";
    std::string query = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'Test'   AND table_name LIKE '" + baseName + "%'"; // Construct key from inputs
    #ifdef DEBUG_PRINT
    errs() << "Executing query: " << query << "\n";
    #endif

    if (mysql_query(conn, query.c_str())) {
        std::cerr << "SELECT query failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "MayAlias"; // Default to MayAlias if query fails
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        std::cerr << "mysql_store_result() failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "MayAlias"; // Default to MayAlias if store result fails
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::string aaValue;
    if (row) {
      #ifdef DEBUG_PRINT
        printf("Table count: %s\n", row[0]);
      #endif
    } else {
        printf("No result.\n");
    }

    aaValue = parallel_query_tables(conn, baseName, key, row);
    aaValue = (aaValue == "") ? parallel_query_tables(conn, baseName, key1, row) : aaValue; //This can be optimized
    #ifdef DEBUG_PRINT
    errs() << "Found sql entry: " << aaValue << "\n";
    #endif
    if(aaValue == ""){
      #ifdef DEBUG_PRINT
      errs() << "No result found in any table, defaulting to MayAlias\n";
      #endif
      aaValue = "MayAlias";
    }
    // if (row != nullptr && row[0] != nullptr) {
    //     aaValue = row[0];
    //     #ifdef DEBUG_PRINT
    //     errs() << "Found sql entry: " << aaValue << "\n";
    //     #endif
    // } else {
    //     std::cerr << "No matching record found.\n";
    // }

    // mysql_free_result(res);
    mysql_close(conn);
    return aaValue;
}

void fetchWindow(PGconn* conn,const std::string& tableName, const std::string& key, std::unordered_map<std::string, std::string> &Cache_svf)
{
    const char* paramValues[1];
    paramValues[0] = key.c_str();

    std::string query = " (SELECT ptr_ptr_func, aa FROM" + tableName + " "
        "   WHERE ptr_ptr_func <= $1 "
        "   ORDER BY ptr_ptr_func DESC LIMIT 10) "
        " UNION ALL "
        " (SELECT ptr_ptr_func, aa FROM" + tableName + " "
        "   WHERE ptr_ptr_func >= $1 "
        "   ORDER BY ptr_ptr_func ASC LIMIT 11)";
    PGresult* res = PQexecParams(
        conn,
        query.c_str(),
        1,
        NULL,
        paramValues,
        NULL,
        NULL,
        0
    );

    int rows = PQntuples(res);

    for(int i=0;i<rows;i++)
    {
        std::string k = PQgetvalue(res,i,0);
        std::string v = PQgetvalue(res,i,1);

        Cache_svf[k] = v;
    }

    PQclear(res);
}
int queryTable(PGconn* conn,
                const std::string& tableName,
                const std::string& key)
{

    const char* params[1];
    int aavalue = -1;
    params[0] = key.c_str();
    std::string query = "SELECT aa FROM " + tableName + " WHERE ptr_ptr_func = $1;";

    PGresult* res = PQexecParams(
        conn,
        query.c_str(),
        1,
        NULL,
        params,
        NULL,
        NULL,
        0
    );
    #ifdef DEBUG_PRINT
    errs() << "Executing query: " << query << "\n";
    #endif

    if (PQntuples(res) == 0) {
      #ifdef DEBUG_PRINT
        errs() << "No matching record found for key: " << key
            << " in table: " << tableName << "\n";
      #endif
        PQclear(res);
        NumSVFsAAUnknown++;
        return 1;
    }
    // PGresult* res = PQexecPrepared(conn,
    //                                "lookup",
    //                                1,
    //                                params,
    //                                NULL,
    //                                NULL,
    //                                0);
    const char* aa = PQgetvalue(res, 0, 0);
    PQclear(res);

    try {
    aavalue = std::stoi(aa);
    } catch (const std::invalid_argument& e) {
        // aa is not a number
    } catch (const std::out_of_range& e) {
        // number too large for int
    }
    #ifdef DEBUG_PRINT
    std::cout << "Alias results: " << aa << std::endl;
   
    std::cout << tableName << " -> " << aa << std::endl;
    #endif
    if (aavalue == 1){
      NumSVFsAAMayAlias++;
    }else if (aavalue == 2){
      NumSVFsAANoAlias++;
    }
    return aavalue;

    // for (int i = 0; i < rows; i++) {
    //     std::string aa = PQgetvalue(res, 0, 0);
    //     std::cout << tableName << " -> " << aa << std::endl;
    //     PQclear(res);
    //     PQfinish(conn);
    //     return aa;
    // }
    // PQclear(res);
    // PQfinish(conn);
}


// std::string fetchAAByPrimaryKey_Spec_multiple_postgress_sql(const std::string &ptr1, const std::string &ptr2, const std::string &moduleName = "", const std::string &functionName = "") {
//   // return "MayAlias";
//   // for a single module with entries above 2M, it is broken down into multiple tables with suffix _1, _2, etc, reading to database should be parallel
//   std::string key; // Assuming this is the primary key column name
//   std::string key1; // Assuming this is the primary key column name
//   if(moduleName == "" || functionName == "" || ptr1 == "" || ptr2 == "") {
//     return "MayAlias"; // Default to MayAlias if module or function name is empty
//   }  
//   omp_set_num_threads(8);
//   // Connect to the database
//   // conninfo is a string of keywords and values separated by spaces.
//   const char* conninfo = "host=localhost dbname=mydb user=aadb_umu password=caps";

//   // Create a connection
//   PGconn *conn = PQconnectdb(conninfo);
//    key = "&" + ptr1 + "@" + "Mem" + "&" + ptr2 + "@" + "Mem"; // Construct key from inputs
//    key1 = "&" + ptr2 + "@" + "Mem" + "&" + ptr1 + "@" + "Mem"; // Construct key from inputs
// #ifdef DEBUG_PRINT
//    errs() << "Querying alias for: " << key << "\n";
//    errs() << "Querying alias for: " << key1 << "\n";
// #endif
//     // Check if the connection is successful
//     if (PQstatus(conn) != CONNECTION_OK) {
//         // If not successful, print the error message and finish the connection
//         printf("Error while connecting to the database server: %s\n", PQerrorMessage(conn));

//         // Finish the connection
//         PQfinish(conn);

//         return "MayAlias"; // Default to MayAlias if connection fails
//     }

//     // We have successfully established a connection to the database server
//     #ifdef DEBUG_PRINT
//     printf("Connection Established\n");
//     printf("Port: %s\n", PQport(conn));
//     printf("Host: %s\n", PQhost(conn));
//     printf("DBName: %s\n", PQdb(conn));
//     #endif
//         // Remove file extension (.ll, .bc, etc.)
//     size_t lastDot = moduleName.find_last_of(".");
//     std::string baseName = moduleName;
//     if (lastDot != std::string::npos) {
//         baseName = moduleName.substr(0, lastDot);
//     }
//     std::string innermost_name = baseName.substr(baseName.find_last_of("/\\") + 1);
//     baseName = innermost_name;
//     // std::string suffix = "_reorderedloads";
//     std::regex suffix("_annotated$");
//     std::regex ihen("-");
//     baseName = std::regex_replace(baseName, suffix, "");
//     baseName = std::regex_replace(baseName, ihen, "__");
//     baseName += "_annotated";

//     std::string query = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_name LIKE '" + baseName + "%'"; // Construct key from inputs
//     #ifdef DEBUG_PRINT
//     errs() << "Executing query: " << query << "\n";
//     errs() << query.c_str() << "\n";
//     #endif
//     // Submit the query and retrieve the result
//     PGresult *res = PQexec(conn, query.c_str());

//     // Check the status of the query result
//     ExecStatusType resStatus = PQresultStatus(res);
//     #ifdef DEBUG_PRINT
//     // Convert the status to a string and print it
//     printf("Query Status: %s\n", PQresStatus(resStatus));
//     #endif
//     // Check if the query execution was successful
//     if (resStatus != PGRES_TUPLES_OK) {
//         // If not successful, print the error message and finish the connection
//         printf("Error while executing the query: %s\n", PQerrorMessage(conn));

//         // Clear the result
//         PQclear(res);

//         // Finish the connection
//         PQfinish(conn);

//         // Exit the program
//         return "MayAlias"; // Default to MayAlias if query fails
//     }
//     #ifdef DEBUG_PRINT
//     // We have successfully executed the query
//     printf("Query Executed Successfully\n");
//     #endif
//     #ifdef DEBUG_PRINT
//     std::cout << "Number of rows returned: " << PQntuples(res) << std::endl;
//     #endif
//     int row = PQntuples(res);
//     std::string aaValue;
//     if (row) {
//       #ifdef DEBUG_PRINT
//         printf("Table count: %s\n", row.c_str());
//       #endif
//     } else {
//         printf("No result.\n");
//         return "MayAlias"; // Default to MayAlias if no result
//     }
//     // Clear the result
//     PQclear(res);

//     // Finish the connection
//     PQfinish(conn);

//     std::atomic<bool> found(false);
//     #pragma omp parallel
//   {
//     PGconn* conn = PQconnectdb(conninfo);
//     #pragma omp for
//     for(int i = 0; i < row; i++) {
//         if(found.load()) continue;  // Early break if already found
//         std::string tableName = baseName + "_" + std::to_string(i+1);
//          std::string val = queryTable(conn, tableName, key);
//           if(!val.empty()) {
//             aaValue = val;
//             found.store(true);
//         }
//     }
//     PQfinish(conn);

//     #ifdef DEBUG_PRINT
//     errs() << "Found sql entry: " << aaValue << "\n";
//     #endif

//     if(aaValue == ""){
//       #ifdef DEBUG_PRINT
//       errs() << "No result found in any table, defaulting to MayAlias\n";
//       #endif
//       aaValue = "MayAlias";
//     }
// }
// return aaValue;
// }

std::string lookup_svf(PGconn *conn, const std::string &tableName,
             const std::string &key, const std::string &key1, std::unordered_map<std::string, std::string> &CacheSVF)
{
    auto it = CacheSVF.find(key);
    auto it1 = CacheSVF.find(key1);

    if(it != CacheSVF.end())
        return it->second;

    if(it1 != CacheSVF.end())
        return it1->second;

  fetchWindow(conn, tableName, key, CacheSVF);
  fetchWindow(conn, tableName, key1, CacheSVF);

    return CacheSVF[key];
}

int fetchAAByPrimaryKey_Spec_postgress_sql(const int &ptr1, const int &ptr2, PGconn *conn, const std::string &moduleName = "", std::unordered_map<std::string, int> *Cache_svf = nullptr) {
  // return "MayAlias";
  // for a single module with entries above 2M, it is broken down into multiple tables with suffix _1, _2, etc, reading to database should be parallel
  std::string key; // Assuming this is the primary key column name
  std::string aaresults;
  // std::string key1; // Assuming this is the primary key column name
  if(moduleName == "") {
    errs() << "Module name is empty, defaulting to MayAlias\n";
    return 1; // Default to MayAlias if module or function name is empty
  }  
  omp_set_num_threads(8);
  int maxMetadataId = -1;
  int minMetadataId = -1;
  // int maxMetadataId = std::max(ptr1, ptr2);
  if (ptr1 > ptr2) {
      maxMetadataId = ptr1;
      minMetadataId = ptr2;
  } else {
      maxMetadataId = ptr2;
      minMetadataId = ptr1;
  }
  if(maxMetadataId == minMetadataId){
    NumSVFsAAMayAlias++;
    return 1; // MayAlias
  }
  // std::cout << "metadataId1: " << ptr1 << ", metadataId2: " << ptr2 << "\n";
  // std::cout << "maxMetadataId: " << maxMetadataId << ", minMetadataId: " << minMetadataId << "\n";
  assert(maxMetadataId != -1 && minMetadataId != -1 && "Invalid metadata IDs");
  key.clear();
  key.reserve(24);
  char buffer[32];
  char* begin = buffer;
  char* end = buffer + sizeof(buffer);

  auto [p1, ec1] = std::to_chars(begin, end, maxMetadataId);
  if (ec1 != std::errc{} || p1 == end) {
    assert(false && "Failed to convert maxMetadataId to string");
  }

  *p1++ = ':';

  auto [p2, ec2] = std::to_chars(p1, end, minMetadataId);
  if (ec2 != std::errc{}) {
      // fallback
    assert(false && "Failed to convert minMetadataId to string");
  }

  key.assign(begin, p2);
  assert(!key.empty() && "Constructed key is empty");
  // std::cout << "Constructed key: " << key << "\n";
  // int minMetadataId = std::min(ptr1, ptr2);
  // key = fmt::format("{}:{}", maxMetadataId, minMetadataId); // Construct key from inputs
  //  key = "&" + maxMetadataId + "@" + "Mem" + "&" + minMetadataId + "@" + "Mem"; // Construct key from inputs
  //  key1 = "&" + ptr2 + "@" + "Mem" + "&" + ptr1 + "@" + "Mem"; // Construct key from inputs"
    #ifdef DEBUG_PRINT
      errs() << "Querying alias for: " << key << "\n";
      errs() << "Querying alias for: " << key1 << "\n";
    #endif
    // errs() << "Querying alias for: " << key << "\n";
    // Check if the connection is successful
    if (PQstatus(conn) != CONNECTION_OK) {
        // If not successful, print the error message and finish the connection
        printf("Error while connecting to the database server: %s\n", PQerrorMessage(conn));

        // Finish the connection
        PQfinish(conn);
        assert(false && "Database connection failed");
        return 0; // Default to MayAlias if connection fails
    }

    // We have successfully established a connection to the database server
    #ifdef DEBUG_PRINT
    printf("Connection Established\n");
    printf("Port: %s\n", PQport(conn));
    printf("Host: %s\n", PQhost(conn));
    printf("DBName: %s\n", PQdb(conn));
    #endif
    // Remove file extension (.ll, .bc, etc.)
    size_t lastDot = moduleName.find_last_of(".");
    std::string baseName = moduleName;
    if (lastDot != std::string::npos) {
        baseName = moduleName.substr(0, lastDot);
    }
    std::string innermost_name = baseName.substr(baseName.find_last_of("/\\") + 1);
    baseName = innermost_name;
    // std::string suffix = "_reorderedloads";
    std::regex suffix("_annotated$");
    std::regex ihen("-");
    std::regex aadb_reorderedloads("_aadb_reorderloads$");
    std::regex reorderedloads("_reorderloads$");
    std::regex aadb("_aadb$");
    std::regex nn("_OR3_unroll_O3$");
    // For example: name1.name2 -> name1_name2 and name1-name2 -> name1__name2
    baseName = std::regex_replace(baseName, suffix, "");
    baseName = std::regex_replace(baseName, ihen, "__");
    baseName = std::regex_replace(baseName, aadb_reorderedloads, "");
    baseName = std::regex_replace(baseName, reorderedloads, "");
    baseName = std::regex_replace(baseName, aadb, "");
    baseName = std::regex_replace(baseName, nn, "");
    std::replace(baseName.begin(), baseName.end(), '.', '_');
    baseName += "_annotated";
    // std::cout << "baseName : " << baseName << "\n";
    // Submit the query and retrieve the result
    auto it = Cache_svf->find(key);
    if(it != Cache_svf->end()) {
        NumSVFsAARepeatedQueries++;
        if(it->second == 1){
          NumSVFsAAMayAlias++;
        }else if(it->second == 2){
          NumSVFsAANoAlias++;
        }
        return it->second;
    }
    #ifdef LOG_compile_time
    const auto dbQueryStart = std::chrono::steady_clock::now();
    #endif
    int aaValue = queryTable(conn, baseName, key);
    #ifdef LOG_compile_time
    const auto dbQueryEnd = std::chrono::steady_clock::now();
    const std::chrono::duration<double> dbQueryElapsed = dbQueryEnd - dbQueryStart;
    errs() << "SVFsAA database query time: " << dbQueryElapsed.count() << " seconds\n";
    #endif
    Cache_svf->insert({key, aaValue});
    #ifdef DEBUG_PRINT
    // Convert the status to a string and print it
    printf("Query Status: %s\n", PQresStatus(resStatus));
    #endif

    #ifdef DEBUG_PRINT
    // We have successfully executed the query
    printf("Query Executed Successfully\n");
    #endif

    #ifdef DEBUG_PRINT
    errs() << "Found sql entry: " << aaValue << "\n";
    #endif

return aaValue;
}

std::string fetchAAByPrimaryKey_Spec_interprocedural(const std::string &ptr1, const std::string &ptr2, const std::string &moduleName = "", const std::string &functionName = "") {
  std::string key; // Assuming this is the primary key column name
  if(moduleName == "" || functionName == "" || ptr1 == "" || ptr2 == "") {
    return "ModRef"; // Default to MayAlias if module or function name is empty
  }  
  MYSQL* conn = mysql_init(nullptr);
   key = "$" + ptr1 + "@" + "Call" + "$" + ptr2 + "@" + "Mem"; // Construct key from inputs
#ifdef DEBUG_PRINT
   errs() << "Querying alias for: " << key << "\n";
#endif
    if (conn == nullptr) {
        std::cerr << "mysql_init() failed\n";
        return "ModRef"; // Default to MayAlias if connection fails
    }

    if (!mysql_real_connect(conn, "localhost", "root", "password", "Test", 0, nullptr, 0)) {
        std::cerr << "mysql_real_connect() failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "ModRef"; // Default to MayAlias if connection fails
    }
        // Remove file extension (.ll, .bc, etc.)
    size_t lastDot = moduleName.find_last_of(".");
    std::string baseName = moduleName;
    if (lastDot != std::string::npos) {
        baseName = moduleName.substr(0, lastDot);
    }
    std::string innermost_name = baseName.substr(baseName.find_last_of("/\\") + 1);
    baseName = innermost_name;
    // std::string suffix = "_reorderedloads";
    std::regex suffix("_annotated$");
    std::regex ihen("-");
    baseName = std::regex_replace(baseName, suffix, "");
    baseName = std::regex_replace(baseName, ihen, "__");
    baseName += "_annotated_interprocedural";
    std::string query = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'Test'   AND table_name LIKE '" + baseName + "%'";
    #ifdef DEBUG_PRINT_INTERPROCEDURAL
    errs() << "Executing query: " << query << "\n";
    #endif

    if (mysql_query(conn, query.c_str())) {
        std::cerr << "SELECT query failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "ModRef"; // Default to MayAlias if query fails
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        std::cerr << "mysql_store_result() failed: " << mysql_error(conn) << "\n";
        mysql_close(conn);
        return "ModRef"; // Default to MayAlias if store result fails
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::string aaValue;
    if (row) {
      #ifdef DEBUG_PRINT
        printf("Table count: %s\n", row[0]);
      #endif
    } else {
        printf("No result.\n");
    }
    aaValue = parallel_query_tables(conn, baseName, key, row);
    #ifdef DEBUG_PRINT_INTERPROCEDURAL
    errs() << "Found sql entry: " << aaValue << "\n";
    #endif
    if(aaValue == ""){
      errs() << "No result found in any table, defaulting to ModRef\n";
      aaValue = "ModRef";
    }
    mysql_free_result(res);
    mysql_close(conn);

    return aaValue;
}

AliasResult SVFsAAResult::alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
																AAQueryInfo &AAQI, const Instruction *CtxI) {

  LLVM_DEBUG(dbgs() << "Executing SVFsAAResult::" << __func__ << "\n"
                    << "On memory locations:\n");
  #ifdef DEBUG_PRINT
  llvm::errs() << "######################Start#########################\n";
  llvm::errs() << "Module name coming from the SVFsAA interface: " << F.getParent()->getName() << "\n";
  #endif
  #ifdef DEBUG_PRINT
  LLVM_DEBUG(LocA.print(dbgs()));
  LLVM_DEBUG(LocB.print(dbgs()));
  LLVM_DEBUG(dbgs() << "\n");
  #endif
  // Module *M = const_cast<Module *>(dyn_cast<Instruction>(LocA.Ptr)->getModule());
  int aaValue = -1;
  std::string moduleNameA = "";
  std::string moduleNameB = "";
  const MDNode* MemID_LocA = LocA.AATags.MemID;
  const MDNode* MemID_LocB = LocB.AATags.MemID;
  Metadata *MemID_LocA_Op1 = MemID_LocA ? MemID_LocA->getOperand(1).get() : nullptr;
  Metadata *MemID_LocB_Op1 = MemID_LocB ? MemID_LocB->getOperand(1).get() : nullptr;
  if(!MemID_LocA || !MemID_LocB) {
    #ifdef DEBUG_PRINT
    errs() << "No MemID metadata found for one of the locations.\n";
    #endif
    NumSVFsAANotFound++;
    // dbgs() << "Location A: ";
    // LocA.print(dbgs());
    // dbgs() << "\n";
    // dbgs() << "Location B: ";
    // LocB.print(dbgs());
    // dbgs() << "\n";
    return AliasResult::MayAlias; // Default to MayAlias if no MemID metadata
  }
  auto *MemID_LocA_ConstMD = dyn_cast<ConstantAsMetadata>(MemID_LocA_Op1);
  auto *MemID_LocB_ConstMD = dyn_cast<ConstantAsMetadata>(MemID_LocB_Op1);
  if (!MemID_LocA_ConstMD || !MemID_LocB_ConstMD) {
    #ifdef DEBUG_PRINT
    errs() << "MemID operand is not a ConstantAsMetadata for one of the locations.\n";
    #endif
    // NumSVFsAANotFound++;
    return AliasResult::MayAlias; // Default to MayAlias if MemID operand is not a ConstantAsMetadata
  }
  auto *MemID_LocA_CI = dyn_cast<ConstantInt>(MemID_LocA_ConstMD->getValue());
  auto *MemID_LocB_CI = dyn_cast<ConstantInt>(MemID_LocB_ConstMD->getValue());
  if (!MemID_LocA_CI || !MemID_LocB_CI) {
    #ifdef DEBUG_PRINT
    errs() << "MemID operand is not a ConstantInt for LocA.\n";
    #endif
    // NumSVFsAANotFound++;
    return AliasResult::MayAlias; // Default to MayAlias if MemID operand is not a ConstantInt
  }
  int MemID_LocA_uint = static_cast<int>(MemID_LocA_CI->getZExtValue());;
  int MemID_LocB_uint = static_cast<int>(MemID_LocB_CI->getZExtValue());
  // if(MemID_LocA_uint % 2 == 0 && MemID_LocB_uint % 2 == 0) {
  //   NumSVFsAALL++;
  //   NumSVFsAAMayAlias++;
  //   return AliasResult::MayAlias; // MayAlias
  // }
  #ifdef DEBUG_PRINT
  errs() << "MemID for LocA: " << MemID_LocA_uint << "\n";
  errs() << "MemID for LocB: " << MemID_LocB_uint << "\n";
  #endif
  //LocA get the metadata of the instruction.
  moduleNameA = F.getParent()->getName().str();
  #ifdef DEBUG_PRINT
  errs() << "Module name for LocA: " << moduleNameA << "\n";
  #endif
  moduleNameB = moduleNameA;
  if(moduleNameA != moduleNameB) {
    assert(moduleNameA == moduleNameB && "Module names should match for both locations");
  }
  assert(moduleNameA != "" && "Module name should not be empty");
  // std::string baseNameA = dyn_cast<Instruction>(LocA.Ptr)->getModule()->getName().str();
  // errs() << "Module name for both locations: " << baseNameA << " , " << baseNameB << "\n";
  //create a postgres connection and query the database for the aa value based on the metadata of the instruction, module name and function name, we can use the same function for both alias and modref query, just need to change the table name in the query.
  static PGconn *conn;
  static int total_number_of_connections = 0;

  if(total_number_of_connections > 95){
    std::string query =
            "SELECT pg_terminate_backend(pid) "
            "FROM pg_stat_activity "
            "WHERE state = 'idle' "
            "AND pid <> pg_backend_pid();";

        PGresult* res = PQexec(conn, query.c_str());

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::cerr << "Query failed: " << PQerrorMessage(conn) << std::endl;
        } else {
            std::cout << "Idle connections terminated." << std::endl;
        }
        total_number_of_connections = 0;
        std::cout << "Total number of connections reset to 0.\n";
        PQclear(res);
  }
  if (!connectToDB) {
    // Connect to the database
    // conninfo is a string of keywords and values separated by spaces.
    const char* conninfo = "host=localhost dbname=mydb user=appuser password=secret";
    // Create a connection
    // std::cout << "Establishing database connection...\n";
    conn = PQconnectdb(conninfo);
    connectToDB = true;
    total_number_of_connections++;
    // std::cout << "Database connection established.\n";
  }

  NumSVFsAAQueries++;
  aaValue = fetchAAByPrimaryKey_Spec_postgress_sql(MemID_LocA_uint, MemID_LocB_uint, conn, moduleNameA, &CacheSVF);
  // std::string aaValue2 = fetchAAByPrimaryKey_Spec_multiple(std::to_string(MemID_LocA_uint), std::to_string(MemID_LocB_uint), moduleNameA, functionName);
  // if((aaValue == "MayAlias" && aaValue2 == "NoAlias")|| (aaValue == "NoAlias" && aaValue2 == "MayAlias")) {
  //   errs() << "Inconsistent AA values from different tables: " << aaValue << " and " << aaValue2 << "\n";
  //   errs() << "MemID_LocA: " << MemID_LocA_uint << ", MemID_LocB: " << MemID_LocB_uint << ", ModuleName: " << moduleNameA << "\n";
  // }
  #ifdef DEBUG_PRINT_Instructions_alias
  errs() << "Fetched AA value from database: " << aaValue << " for MemID " << MemID_LocA_uint << " and MemID " << MemID_LocB_uint << " Module name: " << moduleNameA << "\n";
  errs() << "######################END#########################\n";
  #endif
  if(aaValue == 1) {
    // std::cout << "MAYA" << "\n";
  return AliasResult::MayAlias;
  } else if(aaValue == 2) {
    // std::cout << "NOA" << "\n";
    return AliasResult::NoAlias;
  }else if(aaValue == 0) {
    std::cerr << "Connection to database failed: " << aaValue << "\n";
    assert(false && "Connection to database failed");
  }else {
    std::cerr << "Unexpected AA value from database: " << aaValue << "\n";
    assert(false && "Unexpected AA value from database");
  }
  }


ModRefInfo SVFsAAResult::getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                         AAQueryInfo &AAQI) {
  LLVM_DEBUG(dbgs() << "Executing SVFsAAResult::" << __func__ << "\n"
                    << "On memory locations:\n");
  LLVM_DEBUG(Loc.print(dbgs()));
  LLVM_DEBUG(Call->print(dbgs()));
  return ModRefInfo::ModRef; // Default to ModRef
  // how to use get method of Loc?
  LLVM_DEBUG(dbgs() << "\n");
  uint64_t Metadata_Loc = 0;
  uint64_t Metadata_Call = 0;
  std::string aaValue;
  std::string moduleNameA = "";
  std::string moduleNameB = "";
  std::string functionName = "";
  //LocA is the memory location how to get the Instruction from it? get the metadata of the instruction.
  //how to get the instruction from the memory location? we can only get the pointer operand, we can check if the pointer operand is an instruction, if yes, then we can get the instruction, if not, then we cannot get the instruction, we can only get the pointer operand, in this case, we can only return ModRef as a conservative assumption.
  Loc.print(dbgs());
  if (const auto *I = dyn_cast<Instruction>(Loc.Ptr)) {
    errs() << "Instruction for Loc: " << *I << "\n";
    if ((isa<LoadInst>(I) || isa<StoreInst>(I))) {
      LLVM_DEBUG(dbgs() << "Reading Metadata of LocA.\n");
      Metadata_Loc = GetMemId_Inst(I);
      moduleNameA = I->getModule()->getName().str();
      functionName = I->getFunction()->getName().str();
    }else {
      LLVM_DEBUG(dbgs() << "Loc.Ptr is not a Load or Store instruction.\n");
      return ModRefInfo::ModRef; // Default to ModRef if it's not a load/store, as a conservative assumption
    }
  }else {
    LLVM_DEBUG(dbgs() << "Loc.Ptr is not an instruction.\n");
    return ModRefInfo::ModRef; // Default to ModRef if we cannot get an instruction, as a conservative assumption
  }

  //LocB get the metadata of the instruction.
  if (const auto *I = dyn_cast<Instruction>(Call)) {
    Metadata_Call = GetMemId_Spec_calls(I);
    moduleNameB = I->getModule()->getName().str();
  }

  if(moduleNameA != moduleNameB) {
    errs() << "ModuleNameA: " << moduleNameA << "\n";
    errs() << "ModuleNameB: " << moduleNameB << "\n";
    assert(moduleNameA == moduleNameB && "Module names should match for both locations");
  }

  aaValue = fetchAAByPrimaryKey_Spec_interprocedural(std::to_string(Metadata_Loc), std::to_string(Metadata_Call), moduleNameA, functionName);
  if(aaValue == "NoModRef") {
    return ModRefInfo::NoModRef;
  } else if(aaValue == "Ref") {
    return ModRefInfo::Ref;
  } else if(aaValue == "Mod") {
    return ModRefInfo::Mod;
  } else if(aaValue == "ModRef") {
    return ModRefInfo::ModRef;
  } else {
    assert(false && "Unexpected AA value from database");
  }
}

  ModRefInfo SVFsAAResult::getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI){
    return ModRefInfo::ModRef;
                           };

ModRefInfo SVFsAAResult::getModRefInfoMask(const MemoryLocation &Loc,
                                            AAQueryInfo &AAQI,
                                            bool IgnoreLocals) {
    return ModRefInfo::ModRef;
                               }

  /// Get the location associated with a pointer argument of a callsite.
  ModRefInfo SVFsAAResult::getArgModRefInfo(const CallBase *Call, unsigned ArgIdx){
    return ModRefInfo::ModRef;
  }

  /// Returns the behavior when calling the given call site.
  MemoryEffects SVFsAAResult::getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI){
    return MemoryEffects::unknown();
  }

  /// Returns the behavior when calling the given function. For use when the
  /// call site is not known.
  MemoryEffects SVFsAAResult::getMemoryEffects(const Function *Fn){
    return MemoryEffects::unknown();
  }

AnalysisKey SVFsAA::Key;

SVFsAAResult SVFsAA::run(Function &F, FunctionAnalysisManager &AM) {
  //Add assert to check if the below code is entered
  // assert(false && "Running SVFsAA analysis");
  LLVM_DEBUG(dbgs() << "Running SVFsAA on Function : " << F.getName() << "\n");
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);;

  //Create a map to map memory location to metadata added to memory location, the key is the memory location, the value is the metadata, we can use this map to get the metadata of a memory location in the alias query, and then we can use the metadata to query the database for the alias result.
  SVFsAAResult SAAR = SVFsAAResult(F.getParent()->getDataLayout(), F, TLI);
  return SAAR;
}

char SVFsAAWrapperPass::ID = 0;
void SVFsAAWrapperPass::anchor() {}
INITIALIZE_PASS_BEGIN(SVFsAAWrapperPass, "svfs-aa",
											"SVFs Alias Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(SVFsAAWrapperPass, "svfs-aa",
										"SVFs Alias Analysis", true, true)

FunctionPass *llvm::createSVFsAAWrapperPass() {
	return new SVFsAAWrapperPass();
}

SVFsAAWrapperPass::SVFsAAWrapperPass() : FunctionPass(ID) {
	initializeSVFsAAWrapperPassPass(*PassRegistry::getPassRegistry());
}


bool SVFsAAWrapperPass::runOnFunction(Function &F) {
   auto &TLIWP = getAnalysis<TargetLibraryInfoWrapperPass>();

  Result.reset(new SVFsAAResult(F.getParent()->getDataLayout(), F, TLIWP.getTLI(F)));

  return false;
}

void SVFsAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesAll();
	AU.addRequired<TargetLibraryInfoWrapperPass>();
}
