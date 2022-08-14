#include "sierrachart.h"
#include "colormap.h"

#include <string.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>

SCDLLName("Levels Importer")

void ResetForNextRequest
( SCStudyInterfaceRef sc
, int& r_RequestState
, SCDateTime& r_LastRequestDateTime
, int& r_DrawingsExist
, int& r_FileProcessed
);

int RequestValuesFromServer
( SCStudyInterfaceRef sc
, const SCString& sheetId
, int& r_RequestState
);

void DrawLevel 
( SCStudyInterfaceRef sc
, int lineNumber
, std::vector<char*>& tokens
, int& showPrice
, int& transparencyLevel
, COLORREF& defaultColor);

void ProcessGoogleSheetData
( SCStudyInterfaceRef sc
, const char* sheetId
, int& r_RequestState
, int& showPrice
, int& transparency
, COLORREF& defaultColor);

SCDateTime ParsetDateTimeFromStrings
( SCStudyInterfaceRef sc
, char* dateString
, char* timeString
, bool isEnd);

void ProcessFileData
( SCStudyInterfaceRef sc
, const char* filePath
, int& showPrice
, int& transparency
, COLORREF& defaultColor
, int& r_FileProcessed);

SCString StripQuotes(char* value);

enum DataSource { None, File, GoogleSheet };
const int UNIQUE_LINE_NUM = 9563; // Per ASCIL docs, seed the tool's line number with a random start to avoid clashing with other tools

SCSFExport scsf_GoogleSheetsLevelsImporterV2(SCStudyInterfaceRef sc)
{
  SCString msg;
  
  SCInputRef i_GoogleSheetId = sc.Input[0];  
  SCInputRef i_LocalFilePath = sc.Input[5];
  SCInputRef i_ReloadDaily = sc.Input[10];
  
  // chart related settings
  SCInputRef i_ShowPriceOnChart = sc.Input[15];
  SCInputRef i_Transparency = sc.Input[20];
  SCInputRef i_DefaultColor = sc.Input[25];

  // Set configuration variables
  if (sc.SetDefaults)
  {
    sc.AddMessageToLog("Setting study defaults...", 0);
    sc.GraphName = "Levels Importer";
    sc.GraphRegion = 0;
    sc.AutoLoop = 0;

    i_GoogleSheetId.Name = "Google Sheets ID";
    // REQUIRED:
    //  - you must set SHARING privs to "Anyone with link"
    //  - you must extract the unique key, which is what is here in the URL:
    //  - https://docs.google.com/spreadsheets/d/{ID}/edit?usp=sharing
    //  - For my test/dev sheet, which is at https://docs.google.com/spreadsheets/d/1teaycOH7dQAbBYoSF9GEYmKaPZxdiSHdjC52y7MGm_4/edit?usp=sharing,
    //    the ID is 1teaycOH7dQAbBYoSF9GEYmKaPZxdiSHdjC52y7MGm_4
    i_GoogleSheetId.SetString("1teaycOH7dQAbBYoSF9GEYmKaPZxdiSHdjC52y7MGm_4");

    i_LocalFilePath.Name = "Local File Path (instead of Google Sheet)";
    i_LocalFilePath.SetPathAndFileName("");

    i_Transparency.Name = "Transparency Level";
    i_Transparency.SetInt(70);

    i_ShowPriceOnChart.Name = "Show Price By Chart Label?";
    i_ShowPriceOnChart.SetYesNo(1);

    i_ReloadDaily.Name = "Reload levels daily";
    i_ReloadDaily.SetYesNo(1);
    
    i_DefaultColor.Name = "Default color for drawings";
    i_DefaultColor.SetColor(COLOR_WHITE);

    return;
  }

  SCString& r_HttpResponseContent = sc.GetPersistentSCString(1);
  int& r_FileProcessed = sc.GetPersistentInt(0);
  int& r_RequestState = sc.GetPersistentInt(2);
  SCDateTime& r_LastRequestDateTime = sc.GetPersistentSCDateTime(3);
  int& r_DrawingsExist = sc.GetPersistentInt(0);
  int& r_DataSource = sc.GetPersistentInt(0);

  if (sc.HideStudy && r_DrawingsExist)
  {
    sc.AddMessageToLog("Study hidden, removing drawings and resetting", false);
    sc.DeleteACSChartDrawing(0, TOOL_DELETE_ALL, 0);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist, r_FileProcessed);
    return;
  }
  else if (sc.HideStudy)
  {
    return;
  }

  if (sc.UpdateStartIndex == 0 && (r_RequestState == HTTP_REQUEST_RECEIVED || r_FileProcessed)) // full recalc, so we reload
  {
    sc.AddMessageToLog("Full recalc detected - reprocessing data and drawings.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist, r_FileProcessed);
  }
  else if (r_LastRequestDateTime.IsUnset() || ((sc.CurrentSystemDateTime - r_LastRequestDateTime) >= SCDateTime::MINUTES(1440))) // more than 24 hours since last retrieval
  {
    sc.AddMessageToLog("Refresh interval has passed. Flagging to refresh data.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist, r_FileProcessed);
  }

  if (strcmp(i_LocalFilePath.GetString(), "") == 0 && strcmp(i_GoogleSheetId.GetString(), "") == 0)
  {
    sc.AddMessageToLog("ERROR: Neither file nor sheet is entered. No data to draw.", false);
    return;
  }

  int showPrice = i_ShowPriceOnChart.GetInt();
  int transparency = i_Transparency.GetInt();
  COLORREF defaultColor = i_DefaultColor.GetColor();

  if (strcmp(i_LocalFilePath.GetString(), "") != 0)
  {
    ProcessFileData(sc, i_LocalFilePath.GetString(), showPrice, transparency, defaultColor, r_FileProcessed);
  }  
  if (strcmp(i_GoogleSheetId.GetString(), "") != 0)
  {
    ProcessGoogleSheetData(sc, i_GoogleSheetId.GetString(), r_RequestState, showPrice, transparency, defaultColor);    
  }  
  
  r_DrawingsExist = true;
}

void ResetForNextRequest
( SCStudyInterfaceRef sc
, int& r_RequestState
, SCDateTime& r_LastRequestDateTime
, int& r_DrawingsExist
, int& r_FileProcessed
)
{
  r_RequestState = HTTP_REQUEST_NOT_SENT;
  r_LastRequestDateTime = sc.CurrentSystemDateTime;
  r_DrawingsExist = false;
  r_FileProcessed = false;
  
  sc.DeleteACSChartDrawing(0, TOOL_DELETE_ALL, 0);
}

void ProcessFileData(SCStudyInterfaceRef sc, const char* filePath, int& showPrice, int& transparency, COLORREF& defaultColor, int& r_FileProcessed)
{
  if (r_FileProcessed)
  {
    return;
  }

  SCString msg;
  sc.AddMessageToLog(msg.Format("Local file path is %s.", filePath), false);
  
  std::istringstream buffer;
  
  std::ifstream file(filePath);
  if (!file)
  {
    sc.AddMessageToLog("File not found or unable to be opened.", false);
  }
  else
  {
    std::vector<char*> tokens;
    
    int inputLineIndex = 1;    

    for (std::string line; getline(file, line);) 
    {
      // Skip the header row
      if (inputLineIndex == 1) 
      {
        inputLineIndex++;
        continue;
      }

      SCString scline = line.c_str();  
      scline.Tokenize(",", tokens);
      
      DrawLevel(sc, UNIQUE_LINE_NUM + inputLineIndex, tokens, showPrice, transparency, defaultColor);

      // increment row counter
      inputLineIndex++;
    }
  }
  file.close();

  r_FileProcessed = true;
}

void ProcessGoogleSheetData(SCStudyInterfaceRef sc, const char* sheetId, int& r_RequestState, int& showPrice, int& transparency, COLORREF& defaultColor)
{
  SCString msg;
  
  if (RequestValuesFromServer(sc, sheetId, r_RequestState))
  {
    return; //Return here since we need to wait for the response
  }  
  
  if (sc.HTTPRequestID != 0)//response received
  {
    sc.AddMessageToLog("Response received, rendering tools...", false);
    r_RequestState = HTTP_REQUEST_RECEIVED;
    
    std::vector<char*> tokens;
    // open an input stream and read from our Google Sheet
    std::istringstream input(sc.HTTPResponse.GetChars());

    int inputLineIndex = 1;    

    for (std::string line; getline(input,line);) 
    {
      // Skip the header row
      if (inputLineIndex == 1) 
      {
        inputLineIndex++;
        continue;
      }

      SCString scline = line.c_str();
      scline.Tokenize(",", tokens);
      
      DrawLevel(sc, (UNIQUE_LINE_NUM * 2) + inputLineIndex, tokens, showPrice, transparency, defaultColor);

      // increment row counter
      inputLineIndex++;
    }
  }
}

int RequestValuesFromServer
( SCStudyInterfaceRef sc
, const SCString& sheetId
, int& r_RequestState
)
{
  SCString fullUrl;

  if (r_RequestState != HTTP_REQUEST_NOT_SENT)
  {
    return 0;
  }

  fullUrl.Format("https://docs.google.com/spreadsheets/d/%s/gviz/tq?tqx=out:csv", sheetId.GetChars());
  
  if (!sc.MakeHTTPRequest(fullUrl))
  {
    sc.AddMessageToLog("Error making HTTP request. Check your sheet URL.", true);
    r_RequestState = HTTP_REQUEST_ERROR;
  }
  else
  {
    r_RequestState = HTTP_REQUEST_MADE;
    sc.AddMessageToLog("Requesting data from Google sheet", false);
  }

  return 1;
}

SCDateTime ParsetDateTimeFromStrings (SCStudyInterfaceRef sc, SCString dateString, SCString timeString, bool isEnd = false)
{
  SCDateTime date;
  SCDateTime time;
  SCDateTime dateTime;

  if (strcmp(dateString, "") != 0)
  {
    date = sc.DateStringToSCDateTime(dateString);
    time = sc.TimeStringToSCDateTime(timeString);
    if (isEnd && date.IsDateSet() && time.IsTimeAtMidnight())
    {
      // This date is used as an end date, so pick up the last second of the day
      time.SetTimeHMS(23, 59, 59);
    }
    dateTime.SetDateTime(date.GetDate(), time.GetTime());
  }

  return dateTime;
}

void DrawLevel (SCStudyInterfaceRef sc, int lineNumber, std::vector<char*>& tokens, int& showPrice, int& transparencyLevel, COLORREF& defaultColor)
{  
  SCString msg;
  SCString tokenValue;

  if (tokens.size() >= 12 && strcmp(StripQuotes(tokens.at(11)), "") != 0)
  {
    // We have a value for the hide column, so skip this level
    return;
  }

  s_UseTool Tool;
  Tool.LineStyle = LINESTYLE_SOLID;
  Tool.LineWidth = 1;
  Tool.TextAlignment = DT_RIGHT;
  Tool.ChartNumber = sc.ChartNumber;
  Tool.AddMethod = UTAM_ADD_OR_ADJUST;
  Tool.ShowPrice = showPrice;
  Tool.TransparencyLevel = transparencyLevel;
  Tool.LineNumber = lineNumber;
  
  float price;
  float price2 = 0;

  SCDateTime startDateTime;
  SCDateTime endDateTime;

  SCString note;
  SCString lineType;
  
  int linewidth = 1;
  int textalignment = 1;

  price = atof(StripQuotes(tokens.at(0)));
  Tool.BeginValue = price;

  price2 = atof(StripQuotes(tokens.at(1)));
  if (price2 == 0)
  {
    Tool.DrawingType = DRAWING_HORIZONTALLINE;
    Tool.EndValue = price;
  }
  else
  {
    Tool.DrawingType = DRAWING_RECTANGLE_EXT_HIGHLIGHT;
    Tool.EndValue = price2;
  }

  startDateTime = ParsetDateTimeFromStrings(sc, StripQuotes(tokens.at(2)), StripQuotes(tokens.at(3)));
  Tool.BeginDateTime = startDateTime.IsDateSet() 
    ? startDateTime 
    : sc.BaseDateTimeIn[0];
  
  endDateTime = ParsetDateTimeFromStrings(sc, StripQuotes(tokens.at(4)), StripQuotes(tokens.at(5)), true);
  Tool.EndDateTime = endDateTime.IsDateSet()
    ? endDateTime
    : sc.BaseDateTimeIn[sc.ArraySize - 1];
  if (endDateTime.IsDateSet() && Tool.DrawingType == DRAWING_RECTANGLE_EXT_HIGHLIGHT)
  {
    Tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
  }

  Tool.Text = StripQuotes(tokens.at(6));

  if (strcmp(StripQuotes(tokens.at(7)), "") != 0 && colorMap.count(StripQuotes(tokens.at(7)).GetChars()) > 0) 
  {
    Tool.Color = colorMap[StripQuotes(tokens.at(7)).GetChars()];
    if (price2 > 0) Tool.SecondaryColor = Tool.Color;
  }
  else
  {
    if (strcmp(StripQuotes(tokens.at(7)), "") != 0)
    {
      // This isn't an empty color, it's something we didn't recognize, so notify the user
      msg.Format("Unrecognized color name encountered: %s", StripQuotes(tokens.at(7)));
      sc.AddMessageToLog(msg, false);
    }
    Tool.Color = defaultColor;
  }

  lineType = StripQuotes(tokens.at(8));
  if (lineType.CompareNoCase("solid") == 0) Tool.LineStyle = LINESTYLE_SOLID;
  else if (lineType.CompareNoCase("dash") == 0) Tool.LineStyle = LINESTYLE_DASH;
  else if (lineType.CompareNoCase("dot") == 0) Tool.LineStyle = LINESTYLE_DOT;
  else if (lineType.CompareNoCase("dashdot") == 0) Tool.LineStyle = LINESTYLE_DASHDOT;
  else if (lineType.CompareNoCase("dashdotdot") == 0) Tool.LineStyle = LINESTYLE_DASHDOTDOT;
  else 
  {
    msg.Format("Unknown line type detected: %s. Using default solid line type.", lineType);
    sc.AddMessageToLog(msg, false);
    Tool.LineStyle = LINESTYLE_SOLID;
  }

  if (strcmp(StripQuotes(tokens.at(9)), "") != 0)
  { 
    Tool.LineWidth = atoi(StripQuotes(tokens.at(9)));
  }

  textalignment = atoi(StripQuotes(tokens.at(10)));
  // TODO: change to the string compare, but have to figure out how to handle multiple options
  // Actually, as it turns out, you can only do left or right for study-added tools. This can be simplified.
  if (textalignment > 0) Tool.TextAlignment = textalignment;

  // draw line
  sc.UseTool(Tool);
}

SCString StripQuotes(char* value)
{
  SCString stringValue = value;
  if (stringValue == "\"\"")
  {
    return "";
  }
  if (stringValue.GetSubString(1, 0) == "\"" && stringValue.GetSubString(1, stringValue.GetLength() - 1) == "\"")
  {
    return stringValue.GetSubString(stringValue.GetLength() - 2, 1);
  }
  else
  {
    return stringValue;
  }
}