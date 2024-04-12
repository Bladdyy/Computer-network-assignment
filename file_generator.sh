#!/bin/bash

if [ $# -eq 0 ]; then # jak liczba argumentów równa 0
    echo "Użycie: $0 <rozmiar w MB>"
    exit 1
fi

SIZE=$1

tr -dc 'A-Za-z0-9' < /dev/urandom | head -c $((SIZE * 1000 * 1000)) > "${SIZE}MB.txt"
echo "Done"
