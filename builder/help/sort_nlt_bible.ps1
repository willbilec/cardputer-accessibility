[CmdletBinding()]
param(
    [string]$Source = 'C:\Users\willb\Downloads\ENGNLTO2DA\English_eng_NLT_OT_Drama',
    [string]$Destination = 'D:\Music\Audiobooks\NLT Bible',
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$books = @(
    'Genesis','Exodus','Leviticus','Numbers','Deuteronomy','Joshua','Judges','Ruth',
    '1 Samuel','2 Samuel','1 Kings','2 Kings','1 Chronicles','2 Chronicles','Ezra',
    'Nehemiah','Esther','Job','Psalms','Proverbs','Ecclesiastes','Song of Songs',
    'Isaiah','Jeremiah','Lamentations','Ezekiel','Daniel','Hosea','Joel','Amos',
    'Obadiah','Jonah','Micah','Nahum','Habakkuk','Zephaniah','Haggai','Zechariah','Malachi'
)

if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Source folder not found: $Source"
}
$files = @(Get-ChildItem -LiteralPath $Source -File -Recurse | Where-Object {
    $_.Name -match '^A(?<book>\d{2})___(?<chapter>\d{2,3})_[^\\/]*ENGNLTO2DA\.mp3$'
})
if ($files.Count -eq 0) { throw "No NLT Old Testament MP3 files matched in $Source" }
if ($Apply) { New-Item -ItemType Directory -Path $Destination -Force | Out-Null }

$copied = 0
$skipped = 0
foreach ($file in $files) {
    if ($file.Name -notmatch '^A(?<book>\d{2})___(?<chapter>\d{2,3})_[^\\/]*ENGNLTO2DA\.mp3$') {
        continue
    }
    $bookNumber = [int]$Matches.book
    $chapter = [int]$Matches.chapter
    if ($bookNumber -lt 1 -or $bookNumber -gt $books.Count) {
        Write-Warning "Skipping unknown book number in $($file.Name)"
        continue
    }
    $book = $books[$bookNumber - 1]
    $folder = '{0:D2} - {1}' -f $bookNumber, $book
    $targetName = "Faith Comes By Hearing - $book $chapter.mp3"
    $target = Join-Path (Join-Path $Destination $folder) $targetName
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -eq (Get-FileHash -LiteralPath $target).Hash) {
            $skipped++
            continue
        }
        throw "Refusing to overwrite a different file: $target"
    }
    if ($Apply) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $copied++
    } else {
        Write-Output "$($file.FullName) -> $target"
    }
}
if ($Apply) { Write-Output "Copied $copied chapters; skipped $skipped identical existing files." }
else { Write-Output "Preview only: $($files.Count) matching chapters. Add -Apply to copy." }
