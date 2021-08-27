#ifndef SPIFFSEditor_H_
#define SPIFFSEditor_H_
#include <ESPAsyncWebServer.h>
#include <FS.h>

#ifdef ESP32 
 #define fullName(x) name(x)
#endif

#define SPIFFS_MAXLENGTH_FILEPATH 32
#define MAX_NUM_FILESYSTEMS 3

typedef struct ExcludeListS {
    char *item;
    ExcludeListS *next;
} ExcludeList;

class FsWrapper {
  public:
    FsWrapper(const fs::FS& fs, const String& fsPrefix) :
      _fs(fs), _fsPrefix(fsPrefix) { }

    const String& getFsPrefix() { return _fsPrefix; }

    fs::FS * getFs() { return &_fs; }

    // the below methods are identical to fs::FS class, so FsWrapper can be used just like FS (inheriting from FS is complicated on ESP8266/ESP32)
    //File open(const char* path, const char* mode = FILE_READ);
    virtual File open(const String& path, const char* mode = FILE_READ) { return _fs.open(path, mode); }

    //virtual bool exists(const char* path) { return false; }
    //virtual bool exists(const String& path) { return false; }

    //virtual bool remove(const char* path) { return false; }
    //virtual bool remove(const String& path) { return false; }

    //virtual bool rename(const char* pathFrom, const char* pathTo) { return false; }
    //virtual bool rename(const String& pathFrom, const String& pathTo) { return false; }

    //virtual bool mkdir(const char *path) { return false; }
    //virtual bool mkdir(const String &path) { return false; }

    //virtual bool rmdir(const char *path) { return false; }
    //virtual bool rmdir(const String &path) { return false; }

  protected:
    fs::FS _fs;
    String _fsPrefix;
};

// This class specific to SPIFFSEditor adds printDir and ExcludeList
class FsWrapperSFE : public FsWrapper {
  public:
    FsWrapperSFE(const fs::FS& fs, const String& fsPrefix) :
      FsWrapper(fs, fsPrefix) { }

    // printDir always returns true, indicating it's done printing to printPtr
    virtual bool printDir(Print * printPtr, String path){
      bool firstEntry = true;
      // TODO: double check that path starts with fsPrefix?


#ifdef ESP32
      File dir = _fs.open(path);
#else
      fs::Dir dir = _fs.openDir(path);
#endif

#ifdef ESP32
      File entry = dir.openNextFile();
      while(entry){
#else
      while(dir.next()){
        fs::File entry = dir.openFile("r");
#endif
        String fname = entry.fullName();
        if (fname.charAt(0) != '/') fname = "/" + fname;
        fname = _fsPrefix + fname;
    
#if 1
        if (isExcluded(_fs, fname.c_str())) {
#ifdef ESP32
            entry = dir.openNextFile();
#endif
            continue;
        }
#endif
        if(firstEntry)
          firstEntry = false;
        else
          printPtr->print(',');
        printPtr->print(F("{\"type\":\""));
        printPtr->print(F("file"));
        printPtr->print(F("\",\"name\":\""));
        printPtr->print(String(fname));
        printPtr->print(F("\",\"size\":"));
        printPtr->print(String(entry.size()));
        printPtr->print("}");
#ifdef ESP32
        entry = dir.openNextFile();
#else
        entry.close();
#endif
      }
#ifdef ESP32
      dir.close();
#endif

      return true;
    }

  private:
    static const char * excludeListFile;
    ExcludeList *excludes = NULL;

    bool matchWild(const char *pattern, const char *testee) {
      const char *nxPat = NULL, *nxTst = NULL;

      while (*testee) {
        if (( *pattern == '?' ) || (*pattern == *testee)){
          pattern++;testee++;
          continue;
        }
        if (*pattern=='*'){
          nxPat=pattern++; nxTst=testee;
          continue;
        }
        if (nxPat){ 
          pattern = nxPat+1; testee=++nxTst;
          continue;
        }
        return false;
      }
      while (*pattern=='*'){pattern++;}  
      return (*pattern == 0);
    }

    bool addExclude(const char *item){
      size_t len = strlen(item);
      if(!len){
        return false;
      }
      ExcludeList *e = (ExcludeList *)malloc(sizeof(ExcludeList));
      if(!e){
        return false;
      }
      e->item = (char *)malloc(len+1);
      if(!e->item){
        free(e);
        return false;
      }
      memcpy(e->item, item, len+1);
      e->next = excludes;
      excludes = e;
      return true;
    }

    void loadExcludeList(fs::FS &_fs, const char *filename){
      static char linebuf[SPIFFS_MAXLENGTH_FILEPATH];
      fs::File excludeFile=_fs.open(filename, "r");
      if(!excludeFile){
        //addExclude("/*.js.gz");
        return;
      }  
    #ifdef ESP32
      if(excludeFile.isDirectory()){
        excludeFile.close();
        return;
      }
    #endif
      if (excludeFile.size() > 0){
        uint8_t idx;
        bool isOverflowed = false;
        while (excludeFile.available()){
          linebuf[0] = '\0';
          idx = 0;
          int lastChar;
          do {
            lastChar = excludeFile.read();
            if(lastChar != '\r'){
              linebuf[idx++] = (char) lastChar;
            }
          } while ((lastChar >= 0) && (lastChar != '\n') && (idx < SPIFFS_MAXLENGTH_FILEPATH));  
          if(isOverflowed){
            isOverflowed = (lastChar != '\n');
            continue;
          }
          isOverflowed = (idx >= SPIFFS_MAXLENGTH_FILEPATH);
          linebuf[idx-1] = '\0';
          if(!addExclude(linebuf)){
              excludeFile.close();
              return;
          }
        }
      }
      excludeFile.close();
    }

    bool isExcluded(fs::FS &_fs, const char *filename) {
      if(excludes == NULL){
          loadExcludeList(_fs, String(F("/.exclude.files")).c_str());
      }
      ExcludeList *e = excludes;
      while(e){
        if (matchWild(e->item, filename)){
          return true;
        }
        e = e->next;
      }
      return false;
    }


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

    // the below methods are identical to fs::FS class, so MultiFs can be used just like FS (inheriting from FS is complicated on ESP8266/ESP32)
    //File open(const char* path, const char* mode = FILE_READ);

    File open(const String& path, const char* mode = FILE_READ) {
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(!wrapper)
        return File();

      Serial.print("removePrefixFromPath(");
      Serial.print(path);
      Serial.print("): ");
      Serial.println(removePrefixFromPath(path));

      return wrapper->open(removePrefixFromPath(path), mode);
    }

    virtual bool exists(const char* path) { return false; }
    virtual bool exists(const String& path) { return false; }

    virtual bool remove(const char* path) { return false; }
    virtual bool remove(const String& path) { return false; }

    virtual bool rename(const char* pathFrom, const char* pathTo) { return false; }
    virtual bool rename(const String& pathFrom, const String& pathTo) { return false; }

    virtual bool mkdir(const char *path) { return false; }
    virtual bool mkdir(const String &path) { return false; }

    virtual bool rmdir(const char *path) { return false; }
    virtual bool rmdir(const String &path) { return false; }

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

    fs::FS * getFsFromPath(const String& path){
      FsWrapper * wrapper = getWrapperFromPath(path);
      if(wrapper)
        return wrapper->getFs();
      return NULL;
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

    FsWrapper * _wrappers[MAX_NUM_FILESYSTEMS];
};

// This class specific to SPIFFSEditor adds printDir
class MultiFsSfe : public MultiFs {
  public:
    MultiFsSfe() { 
      memset(_wrappersSfe, 0x00, sizeof(FsWrapperSFE*) * MAX_NUM_FILESYSTEMS);
    }

    // TODO: add deconstructor

    void addWrapper(FsWrapperSFE * wrapper) {
      for(int i=0; i<MAX_NUM_FILESYSTEMS; i++){
        if(_wrappers[i])
          continue;

        _wrappers[i] = (FsWrapper*)wrapper;
        _wrappersSfe[i] = wrapper;
        break;
      }
    }

    FsWrapperSFE * getWrapperFromPath(const String& path) {
      int index = getWrapperIndexFromPath(path);
      if(index < 0)
        return NULL;

      return _wrappersSfe[index];
    }

    // printDir always returns true, indicating it's done printing to printPtr
    bool printDir(Print * printPtr, String path){
      FsWrapperSFE * wrapper = getWrapperFromPath(path);
      if(!wrapper) {
        //Serial.println("!wrapper");
        return true; // TODO: explain printDir return values
      }

      //Serial.print("removePrefixFromPath(");
      //Serial.print(path);
      //Serial.print("): ");
      //Serial.println(removePrefixFromPath(path));

      return wrapper->printDir(printPtr, removePrefixFromPath(path));
    }

  private:
    FsWrapperSFE * _wrappersSfe[MAX_NUM_FILESYSTEMS];
};

class SPIFFSEditor: public AsyncWebHandler {
  private:
    MultiFsSfe _fs;
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
