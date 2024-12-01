usebear=

while getopts "b" opt; do
    case "$opt" in
    b)
        usebear=1
        ;;
    esac
done

shift $((OPTIND-1))

git clean -dfx .

if [[ "$usebear" == "1" ]]; then
  bear -- bash compiledb -n -o ./compile_commands.json make
else
  compiledb -n -o ./compile_commands.json make
fi
