#include "sierrachart.h"
#include "colormap.h"
#include <string>
#include <sstream>
#include <vector>
#include <map>
SCDLLName("Google Sheets Levels Importer V2")

void ResetForNextRequest
( SCStudyInterfaceRef sc
, int& r_RequestState
, SCDateTime& r_LastRequestDateTime
, int& r_DrawingsExist
);

int RequestValuesFromServer
( SCStudyInterfaceRef sc
, const SCString& sheetUrl
, int& r_RequestState
);

void DrawLevels 
( SCStudyInterfaceRef sc
, int& showPrice
, int& transparencyLevel
, COLORREF& defaultColor);

SCDateTime ParsetDateTimeFromStrings
( SCStudyInterfaceRef sc
, char* dateString
, char* timeString
, bool isEnd);

SCSFExport scsf_GoogleSheetsLevelsImporterV2(SCStudyInterfaceRef sc)
{
  SCInputRef i_FilePath = sc.Input[0];
  SCInputRef i_Transparency = sc.Input[1];

  // chart related settings
  SCInputRef i_ShowPriceOnChart = sc.Input[2];
  SCInputRef i_ReloadDaily = sc.Input[3];
  SCInputRef i_DefaultColor = sc.Input[4];

  // Set configuration variables
  if (sc.SetDefaults)
  {
    sc.AddMessageToLog("Setting study defaults...", 0);
    sc.GraphName = "Google Sheets Importer V2";
    sc.GraphRegion = 0;
    sc.AutoLoop = 0;

    i_FilePath.Name = "Google Sheets URL";
    // REQUIRED:
    //    - you must set SHARING privs to "Anyone with link"
    //    - you must strip off anything after the unique key, for example "/edit#usp=sharing" **needs** to be removed
    i_FilePath.SetString("https://docs.google.com/spreadsheets/d/1teaycOH7dQAbBYoSF9GEYmKaPZxdiSHdjC52y7MGm_4");

    i_Transparency.Name = "Transparency Level";
    i_Transparency.SetInt(70);

    i_ShowPriceOnChart.Name = "Show Price By Chart Label?";
    i_ShowPriceOnChart.SetYesNo(0);

    i_ReloadDaily.Name = "Reload levels daily";
    i_ReloadDaily.SetYesNo(1);
    
    i_DefaultColor.Name = "Default color for drawings";
    i_DefaultColor.SetColor(COLOR_WHITE);

    return;
  }

  SCString& r_HttpResponseContent = sc.GetPersistentSCString(1);
  int& r_RequestState = sc.GetPersistentInt(2);
	SCDateTime& r_LastRequestDateTime = sc.GetPersistentSCDateTime(3);
  int& r_DrawingsExist = sc.GetPersistentInt(0);

  if (sc.HideStudy && r_DrawingsExist)
  {
    sc.AddMessageToLog("Study hidden, removing drawings and resetting", false);
    sc.DeleteACSChartDrawing(0, TOOL_DELETE_ALL, 0);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist);
    return;
  }
  else if (sc.HideStudy)
  {
    return;
  }

  if (sc.UpdateStartIndex == 0  && r_RequestState == HTTP_REQUEST_RECEIVED) // full recalc, so we reload
  {
    sc.AddMessageToLog("Full recalc detected - UpdateStartIndex is zero and we have an HTTP request.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist);
  }
  else if (r_LastRequestDateTime.IsUnset() || ((sc.CurrentSystemDateTime - r_LastRequestDateTime) >= SCDateTime::MINUTES(1440))) // more than 24 hours since last retrieval
  {
    sc.AddMessageToLog("Refresh interval has passed. Flagging to request updated data.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime, r_DrawingsExist);
  }

  if (RequestValuesFromServer(sc, i_FilePath.GetString(), r_RequestState))
  {
    return; //Return here since we need to wait for the response
  }
  
  if (sc.HTTPRequestID != 0)//response received
	{
    sc.AddMessageToLog("Response received, rendering tools...", false);
    r_RequestState = HTTP_REQUEST_RECEIVED;
    int showPrice = i_ShowPriceOnChart.GetInt();
    int transparency = i_Transparency.GetInt();
    COLORREF defaultColor = i_DefaultColor.GetColor();
    DrawLevels(sc, showPrice, transparency, defaultColor);
    r_DrawingsExist = true;
	}
}

void ResetForNextRequest
( SCStudyInterfaceRef sc
, int& r_RequestState
, SCDateTime& r_LastRequestDateTime
, int& r_DrawingsExist
)
{
	r_RequestState = HTTP_REQUEST_NOT_SENT;
	r_LastRequestDateTime = sc.CurrentSystemDateTime;
  r_DrawingsExist = false;
}

int RequestValuesFromServer
( SCStudyInterfaceRef sc
, const SCString& sheetUrl
, int& r_RequestState
)
{
  SCString fullUrl;

	if (r_RequestState != HTTP_REQUEST_NOT_SENT)
  {
    return 0;
  }

	fullUrl.Format("%s/gviz/tq?tqx=out:csv", sheetUrl.GetChars());

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

SCDateTime ParsetDateTimeFromStrings (SCStudyInterfaceRef sc, char* dateString, char* timeString, bool isEnd = false)
{
  SCString dateSCString = dateString;
  SCString timeSCString = timeString;
  
  SCDateTime date;
  SCDateTime time;
  SCDateTime dateTime;

  if (strcmp(dateSCString, "") != 0)
  {
    date = sc.DateStringToSCDateTime(dateSCString);
    time = sc.TimeStringToSCDateTime(timeSCString);
    if (isEnd && date.IsDateSet() && time.IsTimeAtMidnight())
    {
      // This date is used as an end date, so pick up the last second of the day
      time.SetTimeHMS(23, 59, 59);
    }
    dateTime.SetDateTime(date.GetDate(), time.GetTime());
  }

  return dateTime;
}

void DrawLevels (SCStudyInterfaceRef sc, int& showPrice, int& transparencyLevel, COLORREF& defaultColor)
{  
  sc.AddMessageToLog("Redrawing levels...", false);
  SCString msg;

  sc.AddMessageToLog("Deleting existing drawings...", false);
  sc.DeleteACSChartDrawing(0, TOOL_DELETE_ALL, 0);

  std::vector<char*> tokens;
  // open an input stream and read from our Google Sheet
  std::istringstream input(sc.HTTPResponse.GetChars());

  int inputLineIndex = 1;
  int uniqueLineNumber = 9563; // Per ASCIL docs, seed the tool's line number with a random start to avoid clashing with other tools

  for (std::string line; getline(input,line);) 
  {
    // Skip the header row
    if (inputLineIndex == 1) 
    {
      inputLineIndex++;
      continue;
    }

    SCString scline = line.c_str();
            
    // skip the opening quotes and end quotes
    scline = scline.GetSubString(scline.GetLength() - 2, 1);    
    
    // anything between quotes and commas
    scline.Tokenize("\",\"", tokens);
    
    if (tokens.size() >= 12 && strcmp(tokens.at(11), "") != 0)
    {
      // We have a value for the hide column, so skip this level
      inputLineIndex++;
      continue;
    }

    s_UseTool Tool;
    Tool.LineStyle = LINESTYLE_SOLID;
    Tool.LineWidth = 1;
    Tool.TextAlignment = DT_RIGHT;
    Tool.ChartNumber = sc.ChartNumber;
    Tool.AddMethod = UTAM_ADD_OR_ADJUST;
    Tool.ShowPrice = showPrice;
    Tool.TransparencyLevel = transparencyLevel;
    Tool.LineNumber = uniqueLineNumber + inputLineIndex;
    
    float price;
    float price2 = 0;

    SCDateTime startDateTime;
    SCDateTime endDateTime;

    SCString note;
    SCString lineType;
    //SCString alignment;
    int linewidth = 1;
    int textalignment = 1;

    price = atof(tokens.at(0));
    Tool.BeginValue = price;

    price2 = atof(tokens.at(1));
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

    startDateTime = ParsetDateTimeFromStrings(sc, tokens.at(2), tokens.at(3));
    Tool.BeginDateTime = startDateTime.IsDateSet() 
      ? startDateTime 
      : sc.BaseDateTimeIn[0];
    
    endDateTime = ParsetDateTimeFromStrings(sc, tokens.at(4), tokens.at(5), true);
    Tool.EndDateTime = endDateTime.IsDateSet()
      ? endDateTime
      : sc.BaseDateTimeIn[sc.ArraySize - 1];
    if (endDateTime.IsDateSet() && Tool.DrawingType == DRAWING_RECTANGLE_EXT_HIGHLIGHT)
    {
      Tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
    }

    Tool.Text = tokens.at(6);

    if (strcmp(tokens.at(7), "") != 0 && colorMap.count(tokens.at(7)) > 0) 
    {
      Tool.Color = colorMap[tokens.at(7)];
      if (price2 > 0) Tool.SecondaryColor = Tool.Color;
    }
    else
    {
      if (strcmp(tokens.at(7), "") != 0)
      {
        // This isn't an empty color, it's something we didn't recognize, so notify the user
        msg.Format("Unrecognized color name encountered: %s", tokens.at(7));
        sc.AddMessageToLog(msg, false);
      }
      Tool.Color = defaultColor;
    }

    lineType = tokens.at(8);
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

    if (strcmp(tokens.at(9), "") != 0)
    { 
      Tool.LineWidth = atoi(tokens.at(9));
    }

    textalignment = atoi(tokens.at(10));
    // TODO: change to the string compare, but have to figure out how to handle multiple options
    // Actually, as it turns out, you can only do left or right for study-added tools. This can be simplified.
    if (textalignment > 0) Tool.TextAlignment = textalignment;

    // draw line
    sc.UseTool(Tool);

    // increment row counter
    inputLineIndex++;
  }
}