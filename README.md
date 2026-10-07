# mftparse-cpp

C++20 port of NTFS-MFT-parser. Reads a raw $MFT dump and prints each record's
$STANDARD_INFORMATION and $FILE_NAME timestamps.

    bash build.sh                      # builds build/mftparse and build/test_mft (g++, -Werror)
    ./build/test_mft                   # unit tests
    ./build/mftparse [-n N] [--format text|csv] [file]   # defaults: MFT.raw, 500 records, text

Text output is byte-identical to the Python tool. -n 0 reads everything.
