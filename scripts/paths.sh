all_paths=
cur_path=
for part in $(echo $1 | sed 's#/# #g')
do
  cur_path="$cur_path$part/"
  all_paths="$cur_path $all_paths"
done
echo "$all_paths" | sed -r 's#\/ # #g'
