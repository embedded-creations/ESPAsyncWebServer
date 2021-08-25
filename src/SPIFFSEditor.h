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

class genericFsWrapper {
    public:

    private:
};

class fsWrapper : public genericFsWrapper {
    public:
        fsWrapper(const fs::FS& fs) : _fs(fs) { }

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
              loadExcludeList(_fs, String(FPSTR(excludeListFile)).c_str());
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

        bool testOpenFile(const char * filename) {
            File tempfile;
            tempfile = _fs.open(filename, "r");
            if(!tempfile){
              return false;
            }
    #ifdef ESP32
            if(tempfile.isDirectory()){
              tempfile.close();
              return false;
            }
    #endif
            tempfile.close();
            return true;
        }

        virtual bool defaultPrintDirFunction(Print * printPtr, String& path){
          #ifdef ESP32
                File dir = _fs.open(path);
          #else
                fs::Dir dir = _fs.openDir(path);
          #endif
                String output = String();

          #ifdef ESP32
                File entry = dir.openNextFile();
                while(entry){
          #else
                while(dir.next()){
                  fs::File entry = dir.openFile("r");
          #endif
              String fname = entry.fullName();
              if (fname.charAt(0) != '/') fname = "/" + fname;
              
                  if (isExcluded(_fs, fname.c_str())) {
          #ifdef ESP32
                      entry = dir.openNextFile();
          #endif
                      continue;
                  }
                  if (output != "[") output += ',';
                  output += F("{\"type\":\"");
                  output += F("file");
                  output += F("\",\"name\":\"");
                  output += String(fname);
                  output += F("\",\"size\":");
                  output += String(entry.size());
                  output += "}";
          #ifdef ESP32
                  entry = dir.openNextFile();
          #else
                  entry.close();
          #endif
                }
          #ifdef ESP32
                dir.close();
          #endif

                printPtr->print(output);

                output = String();
          return true;
        }

    private:
        fs::FS _fs;
};

class SPIFFSEditor: public AsyncWebHandler {
  private:
    fs::FS _fs;
    String _username;
    String _password; 
    bool _authenticated;
    uint32_t _startTime;
    fsWrapper _wrapper;
    String _path;
  public:
#ifdef ESP32
    SPIFFSEditor(const fs::FS& fs, const String& username=String(), const String& password=String());
#else
    //SPIFFSEditor(const String& username=String(), const String& password=String(), const fs::FS& fs=SPIFFS);
    SPIFFSEditor(const String& username, const String& password, const fs::FS& fs); // do not show warning that SPIFFS has been deprecated
#endif
    virtual bool canHandle(AsyncWebServerRequest *request) override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    virtual bool isRequestHandlerTrivial() override final {return false;}
    static bool printDirFromCallback(Print * printPtr, SPIFFSEditor * pThis);
};

#endif
