/*
  TSynth patch saving and recall works like an analogue polysynth from the late 70s (Prophet 5).
  When you recall a patch, all the front panel controls will be different values from those saved in the patch. 
  Moving them will cause a jump to the current value.
*/
//Agileware CircularBuffer available in libraries manager
#include <CircularBuffer.h>
#include "Constants.h"

#define TOTALCHARS 64

const static char CHARACTERS[TOTALCHARS] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ' ', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
int charIndex = 0;
char currentCharacter = 0;
String renamedPatch = "";

struct PatchNoAndName{
  int patchNo;
  String patchName;
};

CircularBuffer<PatchNoAndName, PATCHES_LIMIT> patches;

FLASHMEM size_t readField(File *file, char *str, size_t size, const char *delim){
  char ch;
  size_t n = 0;
  while ((n + 1) < size && file->read(&ch, 1) == 1)
  {
    // Delete CR.
    if (ch == '\r')
    {
      continue;
    }
    str[n++] = ch;
    if (strchr(delim, ch))
    {
      break;
    }
  }
  str[n] = '\0';
  return n;
}

FLASHMEM void recallPatchData(File patchFile, String data[]){
  //Read patch data from file and set current patch parameters
  size_t n;     // Length of returned field with delimiter.
  char str[20]; // Must hold longest field with delimiter and zero byte.
  uint32_t i = 0;
  while (patchFile.available() && i < NO_OF_PARAMS)
  {
    n = readField(&patchFile, str, sizeof(str), ",\n");
    // done if Error or at EOF.
    if (n == 0)
      break;
    // Print the type of delimiter.
    if (str[n - 1] == ',' || str[n - 1] == '\n')
    {
      // Remove the delimiter.
      str[n - 1] = 0;
    }
    else
    {
      // At eof, too long, or read error.  Too long is error.
      Serial.print(patchFile.available() ? F("error: ") : F("eof:   "));
    }
    // Print the field.
    //    Serial.print(i);
    //    Serial.print(" - ");
    //    Serial.println(str);
    data[i++] = String(str);
  }
}

FLASHMEM int compare(const void *a, const void *b) {
  return ((PatchNoAndName*)a)->patchNo - ((PatchNoAndName*)b)->patchNo;
}

FLASHMEM void sortPatches(){
  int arraySize = patches.size();
  //Sort patches buffer to be consecutive ascending patchNo order
  struct PatchNoAndName arrayToSort[arraySize];

  for (int i = 0; i < arraySize; ++i)
  {
    arrayToSort[i] = patches[i];
  }
  qsort(arrayToSort, arraySize, sizeof(PatchNoAndName), compare);
  patches.clear();

  for (int i = 0; i < arraySize; ++i)
  {
    patches.push(arrayToSort[i]);
  }
}

FLASHMEM void loadPatches(){
  LockGuard lock(displayLock); // The display renders from the patch list
  File file = SD.open("/");
  patches.clear();
  while (true)
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    File patchFile = file.openNextFile();
    if (!patchFile)
    {
      break;
    }
    if (patchFile.isDirectory())
    {
      Serial.println("Ignoring Dir");
    }
    else
    {
      recallPatchData(patchFile, data);
      patches.push(PatchNoAndName{atoi(patchFile.name()), data[0]});
      Serial.println(String(patchFile.name()) + ":" + data[0]);
    }
    patchFile.close();
  }
  sortPatches();
}

FLASHMEM void savePatch(const char *patchNo, String patchData){
  // Serial.print("savePatch Patch No:");
  //  Serial.println(patchNo);
  //Overwrite existing patch by deleting
  if (SD.exists(patchNo))
  {
    SD.remove(patchNo);
  }
  File patchFile = SD.open(patchNo, FILE_WRITE);
  if (patchFile)
  {
    //    Serial.print("Writing Patch No:");
    //    Serial.println(patchNo);
    //Serial.println(patchData);
    patchFile.println(patchData);
    patchFile.close();
  }
  else
  {
    Serial.print("Error writing Patch file:");
    Serial.println(patchNo);
  }
}

FLASHMEM void savePatch(const char *patchNo, String patchData[]){
  String dataString = patchData[0];
  for (uint32_t i = 1; i < NO_OF_PARAMS; i++)
  {
    dataString = dataString + F(",") + patchData[i];
  }
  savePatch(patchNo, dataString);
}

FLASHMEM void deletePatch(const char *patchNo)
{
  if (SD.exists(patchNo)) SD.remove(patchNo);
}

FLASHMEM void renumberPatchesOnSD() {
  for (int i = 0; i < patches.size(); i++)
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    File file = SD.open(String(patches[i].patchNo).c_str());
    if (file) {
      recallPatchData(file, data);
      file.close();
      savePatch(String(i + 1).c_str(), data);
    }
  }
  deletePatch(String(patches.size() + 1).c_str()); //Delete final patch which is duplicate of penultimate patch
}

FLASHMEM void setPatchesOrdering(int no) {
  if (patches.size() < 2)return;
  while (patches.first().patchNo != no) {
    patches.push(patches.shift());
  }
}

FLASHMEM void resetPatchesOrdering() {
  setPatchesOrdering(1);
}
