# warc_test

This program is designed to test the Lexbor HTML parser on a large number of HTML pages received from [commoncrawl.org](https://commoncrawl.org/).


## Dependencies

* [zlib](https://zlib.net/)
* [lexbor](https://github.com/lexbor/lexbor) >= 0.3.0


## Build and Installation

```bash
cmake .
```

For build with ASAN:
```bash
cmake . -DCMAKE_C_FLAGS="-O0 -g -fsanitize=address"
```

For link lexbor library from not system path:
```bash
cmake . -DCMAKE_CPP_FLAGS="-I/path/to/include/lexbor" -DCMAKE_EXE_LINKER_FLAGS="-L/path/to/lexbor/lib"
```


## Usage

### warc_test

```text
warc_test <mode> <log file> <directory>
```

```text
<mode>:
    single — one parser on all HTML.
    multi — own parser for each HTML.

<log file>: path to log file.
<directory>: path to directory with *.warc.gz files.
```

For example:
```bash
warc_test single ./warc.log /home/user/warcs
```

### warc_entry_by_index

```text
warc_entry_by_index <index> <file.warc.gz>
```

```text
<index>: starts from 0.
<file.warc.gz>: path to *.warc.gz file.
```

For example:
```bash
warc_entry_by_index 102 /home/user/warcs/CC-MAIN-20190715175205-20190715201205-00354.warc.gz
```


## COPYRIGHT AND LICENSE

   Copyright 2019 Alexander Borisov

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.


Please, see [LICENSE](https://github.com/lexbor/warc_test/blob/master/LICENSE) file.
