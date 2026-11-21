#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:-}"
destination="${2:-}"
apply=0
for arg in "$@"; do
  if [[ "$arg" == "--apply" ]]; then apply=1; fi
done
if [[ -z "$source_dir" || -z "$destination" || "$source_dir" == "--apply" || "$destination" == "--apply" ]]; then
  echo "Usage: $0 SOURCE_FOLDER DESTINATION_FOLDER [--apply]" >&2
  exit 2
fi
if [[ ! -d "$source_dir" ]]; then echo "Source folder not found: $source_dir" >&2; exit 1; fi

books=(Genesis Exodus Leviticus Numbers Deuteronomy Joshua Judges Ruth
  '1 Samuel' '2 Samuel' '1 Kings' '2 Kings' '1 Chronicles' '2 Chronicles' Ezra
  Nehemiah Esther Job Psalms Proverbs Ecclesiastes 'Song of Songs' Isaiah Jeremiah
  Lamentations Ezekiel Daniel Hosea Joel Amos Obadiah Jonah Micah Nahum Habakkuk
  Zephaniah Haggai Zechariah Malachi)
count=0
copied=0
skipped=0
while IFS= read -r -d '' file; do
  name="${file##*/}"
  if [[ "$name" =~ ^A([0-9]{2})___([0-9]{2,3})_[^/]*ENGNLTO2DA\.mp3$ ]]; then
    book_number=$((10#${BASH_REMATCH[1]}))
    chapter=$((10#${BASH_REMATCH[2]}))
    if (( book_number < 1 || book_number > ${#books[@]} )); then
      echo "Skipping unknown book number: $name" >&2
      continue
    fi
    book="${books[$((book_number - 1))]}"
    folder=$(printf '%02d - %s' "$book_number" "$book")
    target="$destination/$folder/Faith Comes By Hearing - $book $chapter.mp3"
    count=$((count + 1))
    if [[ -e "$target" ]]; then
      if cmp -s -- "$file" "$target"; then skipped=$((skipped + 1)); continue; fi
      echo "Refusing to overwrite a different file: $target" >&2
      exit 1
    fi
    if (( apply )); then
      mkdir -p -- "${target%/*}"
      cp -- "$file" "$target"
      copied=$((copied + 1))
    else
      printf '%s -> %s\n' "$file" "$target"
    fi
  fi
done < <(find "$source_dir" -type f -print0)
if (( count == 0 )); then echo "No matching NLT Old Testament MP3 files found." >&2; exit 1; fi
if (( apply )); then echo "Copied $copied chapters; skipped $skipped identical existing files."
else echo "Preview only: $count matching chapters. Add --apply to copy."; fi
