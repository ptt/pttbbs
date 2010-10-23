#!/bin/bash
# This script converts old 'water' to new 'banned' format.
# Before running, create a file with the stamp you want in
# r5149_waterban.data

SRC_SAMPLE=r5149_waterban.data
if [ ! -f "$SRC_SAMPLE" ]; then
  echo "Please touch your timestamp in $SRC_SAMPLE."
  exit 1
fi

i=0
num_boards=$(~/bin/showboard ~/.BRD | wc -l)
boards=$(~/bin/showboard ~/.BRD | sed 's/^ *[0-9][0-9]* //;s/ .*//')
for X in $boards; do
  i=$((i+1))
  echo -n "\rProcessing: $i/$num_boards" >&2
  board_path=$HOME/boards/$(echo $X | cut -b 1)/$X
  #echo "checking: $board_path/water"
  if [ ! -s "$board_path/water" ]; then
      continue
  fi
  water_fn="$board_path/water"
  echo "Processing board: $X"
  uids=$(awk '{print $1}' "$water_fn")
  for uid in $uids; do
    uid_folder="$HOME/home/$(echo $uid | cut -b 1)/$uid"
    if [ -d "$uid_folder" ]; then
      echo "found uid folder: $uid_folder"
    else
      echo "invalid user: $uid"
      continue
    fi
    mkdir -p $uid_folder/banned
    cp $SRC_SAMPLE $uid_folder/banned/b_$X
  done
done
