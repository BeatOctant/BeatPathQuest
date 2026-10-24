# 同梱しているもの

| ファイル | 版 | ライセンス | 取得元 |
|---|---|---|---|
| `nlohmann/json.hpp` | 3.11.3 | MIT（`nlohmann/LICENSE.MIT`。Abseil の Apache-2.0、grisu と UTF-8 decoder の MIT の部分を含む） | https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp |
| `miniz/miniz.c`・`miniz/miniz.h` | 3.0.2 | MIT（`miniz/LICENSE`。miniz.c の一部は Unlicense） | https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip（マップの zip の展開） |
| `doctest/doctest.h` | 2.4.11 | MIT（`doctest/LICENSE.txt`。Catch2 から取った部分は Boost Software License 1.0） | https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h（テストだけが使う） |

コアはゲームにも Quest にも依存しないので、使うライブラリはここに置いて、qpm を通さずにビルドできるようにしている。
