#!/bin/bash
# The compiler warnings in a build log, as annotations on the commit or pull request and a count in
# the job's summary. Warnings don't fail a build here, as they don't on a PC.
#
#   .github/warnings.sh <the folder the compiler ran in> <what was built> < build.log
base=${1:-.}
what=${2:-the build}
ws=${GITHUB_WORKSPACE:-$(pwd)}
n=0
while IFS= read -r line; do
    [[ $line =~ ^([^:]+):([0-9]+):([0-9]+):\ warning:\ (.*)$ ]] || continue
    file=${BASH_REMATCH[1]} row=${BASH_REMATCH[2]} col=${BASH_REMATCH[3]} msg=${BASH_REMATCH[4]}
    case "$file" in
        /*) file=$(realpath -m --relative-to="$ws" "$file") ;;
        *) file=$(realpath -m --relative-to="$ws" "$ws/$base/$file") ;;
    esac
    msg=${msg//%/%25}
    echo "::warning file=$file,line=$row,col=$col::$msg"
    n=$((n + 1))
done < <(grep -E '^[^ :]+:[0-9]+:[0-9]+: warning: ' | sort -u)
echo "$n compiler warnings in $what"
if [ -n "$GITHUB_STEP_SUMMARY" ]; then echo "$n compiler warnings in $what" >> "$GITHUB_STEP_SUMMARY"; fi
