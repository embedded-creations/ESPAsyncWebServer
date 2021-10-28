#include "SPIFFSEditor.h"

#define EDFS

#ifndef EDFS
 #include "edit.htm.gz.h"
#endif

#define LIST_RESPONSE_MAX_DURATION_PER_LOOP_MS  1000
#define SPIFFS_MAXLENGTH_FILEPATH 32
static const char excludeListFile[] PROGMEM = "/.exclude.files";

typedef struct ExcludeListS {
    char *item;
    ExcludeListS *next;
} ExcludeList;

static ExcludeList *excludes = NULL;

static bool matchWild(const char *pattern, const char *testee) {
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

static bool addExclude(const char *item){
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

static void freeExcludeList() {
  ExcludeList * temp;

  while(excludes) {
    temp = excludes;
    excludes = excludes->next;
    free(temp);
  }
}

static void loadExcludeList(fs::FS &_fs, const char *filename){
    static char linebuf[SPIFFS_MAXLENGTH_FILEPATH];
    if(excludes != NULL)
      freeExcludeList();
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

static bool isExcluded(const char *filename) {
  if(excludes == NULL){
      return false;
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

// WEB HANDLER IMPLEMENTATION

#ifdef ESP32
//SPIFFSEditor::SPIFFSEditor(const fs::FS& fs, const String& username, const String& password)
SPIFFSEditor::SPIFFSEditor(const fs::FS& fs, const String& username, const String& password, const String& fsPrefix)
#else
SPIFFSEditor::SPIFFSEditor(const String& username, const String& password, const fs::FS& fs, const String& fsPrefix)
#endif
:_username(username)
,_password(password)
,_authenticated(false)
,_startTime(0)
{
  addFs(fs, fsPrefix);
}

void SPIFFSEditor::addFs(const fs::FS& fs, const String& fsPrefix){
  _fs.addWrapper(new FsWrapperSFE(fs, fsPrefix));
}

bool SPIFFSEditor::testOpenFile(const char * filename) {
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

bool SPIFFSEditor::canHandle(AsyncWebServerRequest *request){
  File tempfile;
  if(request->url().equalsIgnoreCase(F("/edit"))){
    if(request->method() == HTTP_GET){
      if(request->hasParam(F("list")))
        return true;
      if(request->hasParam(F("edit"))){
        if(!testOpenFile(request->arg(F("edit")).c_str()))
          return false;
      }
      if(request->hasParam("download")){
        if(!testOpenFile(request->arg(F("download")).c_str()))
          return false;
      }
      request->addInterestingHeader(F("If-Modified-Since"));
      return true;
    }
    else if(request->method() == HTTP_POST)
      return true;
    else if(request->method() == HTTP_DELETE)
      return true;
    else if(request->method() == HTTP_PUT)
      return true;
  }
  return false;
}


void SPIFFSEditor::handleRequest(AsyncWebServerRequest *request){
  if(_username.length() && _password.length() && !request->authenticate(_username.c_str(), _password.c_str()))
    return request->requestAuthentication();

  if(request->method() == HTTP_GET){
    if(request->hasParam(F("list"))){
      _path = request->getParam(F("list"))->value();

      AsyncWebServerResponse *response = request->beginChunkedStreamResponse("application/json", [](Print *printPtr, size_t index, bool final, void *pThis) -> bool {
        static bool firstEntry;
        static bool donePrinting;

        String& path = ((SPIFFSEditor*)pThis)->_path;
        MultiFs& fs = ((SPIFFSEditor*)pThis)->_fs;
        uint32_t startTime = millis();

        #ifdef ESP32
          static File dir;
          static File entry;
        #else
          static fs::Dir dir;
          static fs::File entry;
        #endif

        // call on disconnect
        if(final) {
          freeExcludeList();
          dir.close();
          entry.close();
          return 1;  // return value of "done sending"
        }

        // index==0 on a new request
        if(!index) {
          donePrinting = false;
          printPtr->print("[");

          if(path.equals("/")) {
            firstEntry = true;
            for(int i=0; i<MAX_NUM_FILESYSTEMS; i++) {
              String prefix = fs.getFsPrefixByIndex(i);
              if(!prefix.length())
                continue;

              if(firstEntry)
                firstEntry = false;
              else
                printPtr->print(',');
              printPtr->print(F("{\"type\":\""));
              printPtr->print(F("file"));
              printPtr->print(F("\",\"name\":\""));
              printPtr->print(prefix);
              printPtr->print(F("\",\"size\":0"));
              printPtr->print("}");
            }
          } else {
            loadExcludeList(*fs.getFsFromPath(path), String(FPSTR(excludeListFile)).c_str());

            firstEntry = true;
            fs.open(path);
            #ifdef ESP32
              dir = fs.open(path);
              entry = dir.openNextFile();
            #else
              dir = fs.openDir(path);
              dir.next();
              entry = dir.openFile("r");
            #endif
          }
        }

        while(entry && (printPtr->availableForWrite() > 255) && (millis() - startTime < LIST_RESPONSE_MAX_DURATION_PER_LOOP_MS)) {
          String fname = entry.fullName(); // this won't have MultiFs prefix
          if (fname.charAt(0) != '/') fname = "/" + fname;

          // check for excluded files before adding MultiFs prefix
          if (isExcluded(fname.c_str())) {
            #ifdef ESP32
              entry = dir.openNextFile();
            #else
              dir.next();
              entry = dir.openFile("r");
            #endif

            continue;
          }

          fname = fs.getFsPrefixFromPath(path) + fname; // add MultiFs Prefix

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
            entry.close();
            entry = dir.openNextFile();
          #else
            entry.close();
            dir.next();
            entry = dir.openFile("r");
          #endif
        }

        if(entry) {
          return 0; // return value of "not done sending", we'll print this entry next pass
        } 

        // we may end up here multiple times after !entry, make sure the closing bracket is only printed once
        if(!donePrinting) {
          donePrinting = true;
          printPtr->print("]"); 
          dir.close();
        }

        return 1; // return value of "done sending"
      }, this);
      request->send(response);
    }
    else if(request->hasParam(F("edit")) || request->hasParam(F("download"))){
      if(request->hasParam(F("edit"))){
        request->_tempFile = _fs.open(request->arg(F("edit")), "r");
        //if(request->_tempFile)
        //  Serial.println("request->_tempFile");
        //else
        //  Serial.println("!request->_tempFile");
      }
      if(request->hasParam("download")){
        request->_tempFile = _fs.open(request->arg(F("download")), "r");
      }

      request->send(request->_tempFile, request->_tempFile.fullName(), String(), request->hasParam(F("download")));
    }
    else {
      const char * buildTime = __DATE__ " " __TIME__ " GMT";
      if (request->header(F("If-Modified-Since")).equals(buildTime)) {
        request->send(304);
      } else {
#ifdef EDFS 
         AsyncWebServerResponse *response = request->beginResponse(_fs, F("/edit_gz"), F("text/html"), false);
#else
         AsyncWebServerResponse *response = request->beginResponse_P(200, F("text/html"), edit_htm_gz, edit_htm_gz_len);
#endif
         response->addHeader(F("Content-Encoding"), F("gzip"));
         response->addHeader(F("Last-Modified"), buildTime);
         request->send(response);
      }
    }
  } else if(request->method() == HTTP_DELETE){
    if(request->hasParam(F("path"), true)){
        if(!(_fs.remove(request->getParam(F("path"), true)->value()))){
#ifdef ESP32
			_fs.rmdir(request->getParam(F("path"), true)->value()); // try rmdir for littlefs
#endif
		}			
			
      request->send(200, "", String(F("DELETE: "))+request->getParam(F("path"), true)->value());
    } else
      request->send(404);
  } else if(request->method() == HTTP_POST){
    if(request->hasParam(F("data"), true, true) && _fs.exists(request->getParam(F("data"), true, true)->value()))
      request->send(200, "", String(F("UPLOADED: "))+request->getParam(F("data"), true, true)->value());
  
	else if(request->hasParam(F("rawname"), true) &&  request->hasParam(F("raw0"), true)){
	  String rawnam = request->getParam(F("rawname"), true)->value();
	  
	  if (_fs.exists(rawnam)) _fs.remove(rawnam); // delete it to allow a mode
	  
	  int k = 0;
	  uint16_t i = 0;
	  fs::File f = _fs.open(rawnam, "a");
	  
	  while (request->hasParam(String(F("raw")) + String(k), true)) { //raw0 .. raw1
		if(f){
			i += f.print(request->getParam(String(F("raw")) + String(k), true)->value());  
		}
		k++;
	  }
	  f.close();
	  request->send(200, "", String(F("IPADWRITE: ")) + rawnam + ":" + String(i));
	  
    } else {
      request->send(500);
    }	  
  
  } else if(request->method() == HTTP_PUT){
    if(request->hasParam(F("path"), true)){
      String filename = request->getParam(F("path"), true)->value();
      if(_fs.exists(filename)){
        request->send(200);
      } else {  
/*******************************************************/
#ifdef ESP32  
		if (strchr(filename.c_str(), '/')) {
			// For file creation, silently make subdirs as needed.  If any fail,
			// it will be caught by the real file open later on
			char *pathStr = strdup(filename.c_str());
			if (pathStr) {
				// Make dirs up to the final fnamepart
				char *ptr = strchr(pathStr, '/');
				while (ptr) {
					*ptr = 0;
					_fs.mkdir(pathStr);
					*ptr = '/';
					ptr = strchr(ptr+1, '/');
				}
			}
			free(pathStr);
		}		  
#endif		  
/*******************************************************/		  
        fs::File f = _fs.open(filename, "w");
        if(f){
          f.write((uint8_t)0x00);
          f.close();
          request->send(200, "", String(F("CREATE: "))+filename);
        } else {
          request->send(500);
        }
      }
    } else
      request->send(400);
  }
}

void SPIFFSEditor::handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    if(!_username.length() || request->authenticate(_username.c_str(),_password.c_str())){
      _authenticated = true;
      request->_tempFile = _fs.open(filename, "w");
      _startTime = millis();
    }
  }
  if(_authenticated && request->_tempFile){
    if(len){
      request->_tempFile.write(data,len);
    }
    if(final){
      request->_tempFile.close();
    }
  }
}
