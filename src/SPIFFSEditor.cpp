#include "SPIFFSEditor.h"

#define EDFS

#ifndef EDFS
 #include "edit.htm.gz.h"
#endif

const char * fsWrapper::excludeListFile PROGMEM = "/.exclude.files";



// WEB HANDLER IMPLEMENTATION

#ifdef ESP32
SPIFFSEditor::SPIFFSEditor(const fs::FS& fs, const String& username, const String& password)
#else
SPIFFSEditor::SPIFFSEditor(const String& username, const String& password, const fs::FS& fs)
#endif
:_fs(fs)
,_username(username)
,_password(password)
,_authenticated(false)
,_startTime(0)
{
  // clear all wrappers, then assign arguments to the first one only
  memset(_wrappers, 0x00, sizeof(fsWrapper*) * MAX_NUM_FILESYSTEMS);
  _wrappers[0] = new fsWrapper(fs, fsPrefix);
}

fsWrapper * SPIFFSEditor::getWrapperFromPath(const String& path) {
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
      return _wrappers[i];
    }

    Serial.print("!= ");
    Serial.println(fsPrefix);
  }
  return NULL;
}

fs::FS * SPIFFSEditor::getFsFromPath(const String& path){
  fsWrapper * wrapper = getWrapperFromPath(path);
  if(wrapper)
    return wrapper->getFs();
  return NULL;
}

String SPIFFSEditor::removePrefixFromPath(const String& path){
  fsWrapper * wrapper = getWrapperFromPath(path);
  if(!wrapper) return String();

  String shortpath = path;
  // remove prefix from path
  shortpath.remove(0, wrapper->getFsPrefix().length());
  if (shortpath.charAt(0) != '/') shortpath = "/" + shortpath;
  return shortpath;
}

File SPIFFSEditor::open(const String& path, const char* mode){
  fs::FS * fsPtr = getFsFromPath(path);
  if(!fsPtr)
    return File();

  //Serial.print("removePrefixFromPath(");
  //Serial.print(path);
  //Serial.print("): ");
  //Serial.println(removePrefixFromPath(path));

  return fsPtr->open(removePrefixFromPath(path), mode);
}

void SPIFFSEditor::addFs(fs::FS& fs, const String& fsPrefix, PrintDirFunction altListDirFunction){
  for(int i=0; i<MAX_NUM_FILESYSTEMS; i++){
    if(_wrappers[i])
      continue;

    _wrappers[i] = new fsWrapper(fs, fsPrefix);
    break;
  }
}

bool SPIFFSEditor::testOpenFile(const char * filename) {
  File tempfile;
  tempfile = open(filename, "r");
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

bool SPIFFSEditor::printDirFromCallback(Print * printPtr, SPIFFSEditor * pThis){
  // TODO: go through wrappers, finding first wrapper that matches prefix, and printing prefixes on "/"
  fsWrapper * wrapper = pThis->getWrapperFromPath(pThis->_path);  

  if(!wrapper) {
    // print list of wrapper paths when requesting root directory
    if(pThis->_path.equals("/")) {
      bool firstEntry = true;
      for(int i=0; i<MAX_NUM_FILESYSTEMS; i++) {
        fsWrapper * wrapper = pThis->_wrappers[i];
        if(!wrapper)
          continue;

        if(firstEntry)
          firstEntry = false;
        else
          printPtr->print(',');
        printPtr->print(F("{\"type\":\""));
        printPtr->print(F("file"));
        printPtr->print(F("\",\"name\":\""));
        printPtr->print(wrapper->getFsPrefix());
        printPtr->print(F("\",\"size\":0"));
        printPtr->print("}");
      }
    }
    return true;
  }

  wrapper->printDir(printPtr, pThis->_path);

  return true;
}

void SPIFFSEditor::handleRequest(AsyncWebServerRequest *request){
  if(_username.length() && _password.length() && !request->authenticate(_username.c_str(), _password.c_str()))
    return request->requestAuthentication();

  if(request->method() == HTTP_GET){
    if(request->hasParam(F("list"))){
      String path = request->getParam(F("list"))->value();
      _path = request->getParam(F("list"))->value();

      AsyncWebServerResponse *response = request->beginStreamRepeaterResponse("application/json", [](Print *printPtr, size_t index, bool final, void *pThis) -> bool {

        if(!index) {
          //Serial.println("first callback");
        }

        // call on disconnect
        if(final) {
          //Serial.println("final");
          return 1;  // return value of "done sending"
        }

        // each time this is called, we print the entire response, but AsyncChunkedStreamResponse knows to ignore what has already been sent, and fills in just what is new until the buffer is full
        printPtr->print("[");
        printDirFromCallback(printPtr, (SPIFFSEditor*)pThis);
        printPtr->print("]");

        return 1;  // return value of "done sending"
      }, this);
      request->send(response);
    }
    else if(request->hasParam(F("edit")) || request->hasParam(F("download"))){
      if(request->hasParam(F("edit"))){
        request->_tempFile = open(request->arg(F("edit")), "r");
      }
      if(request->hasParam("download")){
        request->_tempFile = open(request->arg(F("download")), "r");
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
