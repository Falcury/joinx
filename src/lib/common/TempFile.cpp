#include "TempFile.hpp"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <cstdio> // P_tmpdir in glibc
#include <cstdlib>
#include <stdexcept>

using namespace std;
using boost::format;
namespace bfs = boost::filesystem;

// TempFile ///////////////////////////// 

const char* TempFile::sys_tmpdir() {
#ifdef P_tmpdir
    const char* def = P_tmpdir;
#else
    const char* def = "/tmp";
#endif

    const char* envtmp = getenv("TMPDIR");
    return envtmp == NULL ? def : envtmp;
}

TempFile::ptr TempFile::create(Mode mode) {
    return ptr(new TempFile(mode));
}

TempFile::ptr TempFile::create(const std::string& tmpl, Mode mode) {
    return ptr(new TempFile(tmpl, mode));
}

TempFile::TempFile(Mode mode)
    : _path(sys_tmpdir())
{
}

TempFile::TempFile(const std::string& tmpl, Mode mode)
    : _path(tmpl)
{
    _mkstemp();
}

TempFile::~TempFile() {
    if (_mode == CLEANUP)
        bfs::remove(_path);
}

void TempFile::_mkstemp() {
    int fd = mkstemp(&_path[0]);
    if (fd > 0) {
        close(fd);
        _stream.open(_path.c_str(), ios::in|ios::out|ios::binary);
    }

    if (!_stream.is_open())
        throw runtime_error(str(format("Failed to create temp file with template %1%") %_path));

    if (_mode == ANON) {
        bfs::remove(_path);
        _path.clear();
    }
}

// TempDir ////////////////////////////// 

TempDir::ptr TempDir::create(Mode mode) {
    return ptr(new TempDir(mode));
}

TempDir::ptr TempDir::create(const std::string& tmpl, Mode mode) {
    return ptr(new TempDir(tmpl, mode));
}

TempDir::TempDir(Mode mode)
    : _path(TempFile::sys_tmpdir())
    , _mode(mode)
{
    _path += "/XXXXXX";
    _mkdtemp();
}

TempDir::TempDir(const std::string& tmpl, Mode mode)
    : _path(tmpl)
    , _mode(mode)
{
    _mkdtemp();
}

TempDir::~TempDir() {
    if (_mode == CLEANUP)
        bfs::remove_all(_path);
}

TempFile::ptr TempDir::tempFile(TempFile::Mode mode) const {
    return TempFile::create(_path + "/tmp.XXXXXX", mode);
}

void TempDir::_mkdtemp() {
    if (mkdtemp(&_path[0]) == NULL)
        throw runtime_error(str(format("Failed to create temp dir with template %1%") %_path));
}

