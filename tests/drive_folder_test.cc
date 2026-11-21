#include <fstream>
#include <sstream>

#include "check.h"
#include "drive_folder.h"

static std::string Read(const char* name) {
    std::ifstream f(std::string(FIXTURES_DIR) + "/" + name);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    auto r = drive::ParseLink("https://drive.google.com/drive/folders/AbCdEfGhIjKlMnOp_q-1?resourcekey=0-xyz_1&usp=sharing");
    CHECK(r.id == "AbCdEfGhIjKlMnOp_q-1" && r.resource_key == "0-xyz_1");
    CHECK(drive::ParseLink("  AbCdEfGhIjKlMnOp  ").id == "AbCdEfGhIjKlMnOp");
    CHECK(drive::ParseLink("open?id=AbCdEfGhIjKlMnOp").id == "AbCdEfGhIjKlMnOp");
    CHECK(drive::ParseLink("short").empty());
    CHECK(drive::ListUrl(r) == "https://drive.google.com/embeddedfolderview?id=AbCdEfGhIjKlMnOp_q-1&resourcekey=0-xyz_1");
    CHECK(drive::DownloadUrl({"FILEid", ""}) ==
          "https://drive.usercontent.google.com/download?id=FILEid&export=download&confirm=t");

    drive::Folder f;
    CHECK(drive::ParseFolderHtml(Read("drive_folder.html"), f));
    CHECK(f.title == "Sách của tôi & bạn");
    CHECK(f.items.size() == 3);
    CHECK(f.items[0].folder && f.items[0].name == "Truyện ngắn" && f.items[0].ref.resource_key == "0-KEYabc_123");
    CHECK(!f.items[1].folder && f.items[1].ref.id == "FILEid000000000001");
    CHECK(f.items[1].ref.resource_key == "0-FKEY_9");  // from &amp;resourcekey=
    CHECK(f.items[1].name == "Dế Mèn phiêu lưu ký 'bản đẹp'.EPUB");
    CHECK(drive::IsReadable(f.items[1].name));
    CHECK(!drive::IsReadable(f.items[2].name));
    CHECK(!drive::ParseFolderHtml("<html>Sign in</html>", f));

    CHECK(drive::SafeFileName("a/b:c?.epub") == "a_b_c_.epub");
    CHECK(drive::SafeFileName("  ..  ") == "book");
    CHECK(drive::DecodeEntities("&#7871;&#x1EBF;&lt;") == "ếế<");
    DONE();
}
