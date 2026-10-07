# mftparse-cpp

C++20 port of NTFS-MFT-parser. Reads a raw $MFT dump and prints each record's
$STANDARD_INFORMATION and $FILE_NAME timestamps.

    bash build.sh                      # builds build/mftparse and build/test_mft (g++, -Werror)
    ./build/test_mft                   # unit tests
    ./build/mftparse [-n N] [--format text|csv] [file]   # defaults: MFT.raw, 500 records, text

Text output is byte-identical to the Python tool. -n 0 reads everything.

## Performance

mftparse maps the file (Win32 MapViewOfFile / POSIX mmap), formats records on a worker
pool and writes the output in file order, so results are identical for any `-t` value.
Measured on a 12-thread machine with a 205 MB synthetic $MFT (39 MB of text output):

    Python tool            4.4 s
    C++ v1 (ifstream)      0.78 s
    C++ mmap, -t 1         0.38 s
    C++ mmap, -t 12        0.14 s   (~32x the Python tool)

## Fixups

NTFS overwrites the last two bytes of every 512-byte sector of a record with an update
sequence number. mftparse restores the original bytes from the update sequence array before
parsing, so names that straddle a sector end are read correctly. (The original Python tool
does not do this and returns garbled names for such records.) Verified on the 13 MB sample
$MFT from github.com/omerbenamram/mft: 31,570 timestamp items, all matching that
independent parser except $Secure, whose zero FILETIMEs are printed as None, as in the
Python tool.
