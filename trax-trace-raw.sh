#!/bin/bash

base=`dirname "$0"`
sed 's/^.*trax_dump: //' | "$base/undump" | "$base/trax-decode" | "$base/trax-trace.sh" "$@" --trax-log -
