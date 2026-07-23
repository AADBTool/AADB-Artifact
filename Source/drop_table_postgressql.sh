#!/bin/bash

PGPASSWORD="secret" psql -h localhost -U appuser -d mydb \
-c "SELECT tablename FROM pg_tables WHERE schemaname='public';" -t |
while read t; do
    if [ -n "$t" ]; then
        echo "Size of the table $t ..."
        PGPASSWORD="secret" psql -h localhost -U appuser -d mydb \
        -c "SELECT pg_size_pretty(pg_relation_size('$t'));" \
        -c "DROP TABLE IF EXISTS \"$t\" CASCADE;"
        echo "Table $t dropped."
    fi
done