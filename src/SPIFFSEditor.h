#ifndef SPIFFSEditor_H_
#define SPIFFSEditor_H_
#include <ESPAsyncWebServer.h>
#include <FS.h>

#ifdef ESP32 
 #define fullName(x) name(x)
#endif

#define MAX_NUM_FILESYSTEMS 3

class FsWrapper {
  public:
    FsWrapper(const fs::FS& fs, const String& fsPrefix) :
      _fs(fs), _fsPrefix(fsPrefix) { }

    const String& getFsPrefix() { return _fsPrefix; }

    fs::FS * getFs() { return &_fs; }

    // the below methods are identical to fs::FS class, so FsWrapper can be used just like FS (inheriting from FS is complicated on ESP8266/ESP32)
    virtual File open(const char* path, const char* mode = FILE_READ) { return _fs.open(path, mode); }
    virtual File open(const String& path, const char* mode = FILE_READ) { return _fs.open(path, mode); }

    virtual bool exists(const char* path) { return _fs.exists(path); }
    virtual bool exists(const String& path) { return _fs.exists(path); }

    virtual bool remove(const char* path) { return _fs.remove(path); }
    virtual bool remove(const String& path) { return _fs.remove(path); }

    virtual bool rename(const char* pathFrom, const char* pathTo) { return _fs.rename(pathFrom, pathTo); }
    virtual bool rename(const String& pathFrom, const String& pathTo) { return _fs.rename(pathFrom, pathTo); }

    virtual bool mkdir(const char *path) { return _fs.mkdir(path); }
    virtual bool mkdir(const String &path) { return _fs.mkdir(path); }

    virtual bool rmdir(const char *path) { return _fs.rmdir(path); }
    virtual bool rmdir(const String &path) { return _fs.rmdir(path); }

  protected:
    fs::FS _fs;
    String _fsPrefix;
};

// This class specific to SPIFFSEditor adds printDir and ExcludeList
class FsWrapperSFE : public FsWrapper {
  public:
    FsWrapperSFE(const fs::FS& fs, const String& fsPrefix) :
      FsWrapper(fs, fsPrefix) { }
};

class MultiFs {
  public:
    MultiFs() { 
      memset(_wrappers, 0x00, sizeof(FsWrapper*) * MAX_NUM_FILESYSTEMS);
    }

    // TODO: add deconstructor

    void addWrapper(FsWrapper * wrapper) {
      for(int i=0; i<MAX_NUM_FILESYSTEMS; i++){
        if(_wrappers[i])
          continue;

        _wrappers[i] = wrapper;
        break;
      }
    }

    void addFs(const fs::FS& fs, const String& fsPrefix){
      for(int i=0; i<MAX_NUM_FILESYSTEMS; i++){
        if(_wrappers[i])
          continue;

        _wrappers[i] = new FsWrapper(fs, fsPrefix);
        break;
      }
    }

    const String getFsPrefixByIndex(int index) {
      if (index < 0 || index > MAX_NUM_FILESYSTEMS || !_wrappers[index])
        return String();

      return _wrappers[index]->getFsPrefix();
    }

    const String getFsPrefixFromPath(const String& path) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return String();
      return wrapper->getFsPrefix();
    }

    String removePrefixFromPath(const String& path){
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper) return String();

      String shortpath = path;
      // remove prefix from path
      shortpath.remove(0, wrapper->getFsPrefix().length());
      if (shortpath.charAt(0) != '/') shortpath = "/" + shortpath;
      return shortpath;
    }

    fs::FS * getFsFromPath(const String& path){
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(wrapper)
        return wrapper->getFs();
      return NULL;
    }

    // the below methods are identical to fs::FS class, so MultiFs can be used just like FS (inheriting from FS is complicated on ESP8266/ESP32)
    File open(const char* path, const char* mode = FILE_READ) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return File();

      Serial.print("removePrefixFromPath(");
      Serial.print(path);
      Serial.print("): ");
      Serial.println(removePrefixFromPath(path));

      return wrapper->open(removePrefixFromPath(path), mode);
    }
    File open(const String& path, const char* mode = FILE_READ) { return open(path.c_str(), mode); }

    virtual bool exists(const char* path) { 
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return false;

      return wrapper->exists(removePrefixFromPath(path));
    }
    virtual bool exists(const String& path) { return exists(path.c_str()); }

    virtual bool remove(const char* path) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return false;
      return wrapper->remove(removePrefixFromPath(path));
    }
    virtual bool remove(const String& path) { return remove(path.c_str()); }

    virtual bool rename(const char* pathFrom, const char* pathTo) { 
      // TODO: check pathFrom and pathTo use the same wrapper?
      FsWrapper * wrapper = getWrapperFromPath(pathFrom);
      if(!wrapper)
        return false;
      return wrapper->rename(removePrefixFromPath(pathFrom), removePrefixFromPath(pathTo));
    }
    virtual bool rename(const String& pathFrom, const String& pathTo) { return rename(pathFrom.c_str(), pathTo.c_str()); }

    virtual bool mkdir(const char *path) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return false;
      return wrapper->mkdir(removePrefixFromPath(path));
    }
    virtual bool mkdir(const String &path) { return mkdir(path.c_str()); }

    virtual bool rmdir(const char *path) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return false;
      return wrapper->rmdir(removePrefixFromPath(path));
    }
    virtual bool rmdir(const String &path) { return rmdir(path.c_str()); }

  protected:
    int getWrapperIndexFromPath(const String& path) {
      // go through wrappers, looking for matching fsPrefix
      //Serial.print("getWrapperFromPath(");
      //Serial.print(path);
      //Serial.print("): ");

      for(int i=0; i<MAX_NUM_FILESYSTEMS; i++){
        if(!_wrappers[i])
          continue;

        String fsPrefix = _wrappers[i]->getFsPrefix();
        String pathPrefix = path.substring(0,fsPrefix.length());
        if(pathPrefix.equalsIgnoreCase(fsPrefix)) {
          Serial.print("== ");
          Serial.println(fsPrefix);
          return i;
        }

        Serial.print("!= ");
        Serial.println(fsPrefix);
      }
      return -1;
    }

    FsWrapper * getWrapperFromPath(const String& path) {
      int index = getWrapperIndexFromPath(path);
      if(index < 0)
        return NULL;

      return _wrappers[index];
    }

    FsWrapper * _wrappers[MAX_NUM_FILESYSTEMS];
};

class SPIFFSEditor: public AsyncWebHandler {
  private:
    MultiFs _fs;
    String _username;
    String _password; 
    bool _authenticated;
    uint32_t _startTime;
    String _path;
    //char * rootName = "/":

    bool testOpenFile(const char * filename);

  public:
#ifdef ESP32
    SPIFFSEditor(const fs::FS& fs, const String& username=String(), const String& password=String(), const String& fsPrefix=String());
#else
    SPIFFSEditor(const String& username, const String& password, const fs::FS& fs, const String& fsPrefix=String());
#endif
    void addFs(const fs::FS& fs, const String& fsPrefix);
    virtual bool canHandle(AsyncWebServerRequest *request) override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    virtual bool isRequestHandlerTrivial() override final {return false;}
    static bool printDirFromCallback(Print * printPtr, SPIFFSEditor * pThis);
};

#endif
