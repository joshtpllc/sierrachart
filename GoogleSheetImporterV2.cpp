/*
====== NOTES ======
SCString has a case-insensitve compare - SCString::CompareNoCase()
REF: https://www.sierrachart.com/index.php?page=doc/ACSILProgrammingConcepts.html#WorkingWithTextStrings

You can delete all the study drawings with sc.DeleteACSChartDrawing(0, TOOL_DELETE_ALL, 0)
REF: https://www.sierrachart.com/index.php?page=doc/ACSILDrawingTools.html#scDeleteACSChartDrawing

Manual looping must use sc.UpdateStartIndex. You know it's the first run if that's 0, which is where we'd likely hit the HTTP endpoint
REF: https://www.sierrachart.com/index.php?page=doc/ACS_ArraysAndLooping.html#ManualLooping

If you use pointers to store the response data (not sure if that's needed, but the example uses it), then you'll need to clean up
when the study is removed from the chart. You'll know that based on the sc.LastCallToFunction being 1/true
REF: https://www.sierrachart.com/index.php?page=doc/ACSIL_Members_Variables_And_Arrays.html#scLastCallToFunction
*/

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
);

int RequestValuesFromServer
( SCStudyInterfaceRef sc
, const SCString& sheetUrl
, int& r_RequestState
);

void DrawLevels 
( SCStudyInterfaceRef sc
, int& showPrice
, int& transparencyLevel);

SCDateTime ParsetDateTimeFromStrings
( SCString& dateString
, SCString& timeString);

SCSFExport scsf_GoogleSheetsLevelsImporterV2(SCStudyInterfaceRef sc)
{
  SCInputRef i_FilePath = sc.Input[0];
  SCInputRef i_Transparency = sc.Input[1];

  // chart related settings
  SCInputRef i_ShowPriceOnChart = sc.Input[2];
  SCInputRef i_ReloadDaily = sc.Input[3];

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

    return;
  }

  SCString& r_HttpResponseContent = sc.GetPersistentSCString(1);
  int& r_RequestState = sc.GetPersistentInt(2);
	SCDateTime& r_LastRequestDateTime = sc.GetPersistentSCDateTime(3);

  if (sc.UpdateStartIndex == 0  && r_RequestState == HTTP_REQUEST_RECEIVED) // full recalc, so we reload
  {
    sc.AddMessageToLog("Full recalc detected - UpdateStartIndex is zero and we have an HTTP request.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime);
  }
  else if (r_LastRequestDateTime.IsUnset() || ((sc.CurrentSystemDateTime - r_LastRequestDateTime) >= SCDateTime::MINUTES(1440))) // more than 24 hours since last retrieval
  {
    sc.AddMessageToLog("Refresh interval has passed. Flagging to request updated data.", false);
    ResetForNextRequest(sc, r_RequestState, r_LastRequestDateTime);
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
    DrawLevels(sc, showPrice, transparency);
	}
}

void ResetForNextRequest
( SCStudyInterfaceRef sc
, int& r_RequestState
, SCDateTime& r_LastRequestDateTime
)
{
	r_RequestState = HTTP_REQUEST_NOT_SENT;
	r_LastRequestDateTime = sc.CurrentSystemDateTime;
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

void DrawLevels (SCStudyInterfaceRef sc, int& showPrice, int& transparencyLevel)
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

    s_UseTool Tool;
    Tool.LineStyle = LINESTYLE_SOLID;
    Tool.LineWidth = 1;
    Tool.TextAlignment = DT_RIGHT;

    int idx = 1;
    float price;
    float price2 = 0;

    SCString startDateString;
    SCDateTime startDate;
    SCString startTimeString;
    SCDateTime startTime;
    SCDateTime startDateTime;

    SCString endDateString;
    SCDateTime endDate;
    SCString endTimeString;
    SCDateTime endTime;
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

    startDateString = tokens.at(2);
    startTimeString = tokens.at(3);
    endDateString = tokens.at(4);
    endTimeString = tokens.at(5);

    if (startDateString != "")
    {
      startDate = sc.DateStringToSCDateTime(startDateString);
      startTime = sc.TimeStringToSCDateTime(startTimeString);
      startDateTime.SetDateTime(startDate.GetDate(), startTime.GetTime());
      if (startDateTime.IsDateSet())
      {
        Tool.BeginDateTime = startDateTime;
      }
      else
      {
        Tool.BeginDateTime = sc.BaseDateTimeIn[0];    
      }
    }
    else
    {
      Tool.BeginDateTime = sc.BaseDateTimeIn[0];    
    }

    if (endDateString != "")
    {
      endDate = sc.DateStringToSCDateTime(endDateString);
      endTime = sc.TimeStringToSCDateTime(endTimeString);
      if (endDate.IsDateSet() && endTime.IsTimeAtMidnight())
      {
        // This is a full-day drawing, so we will use 11:59:59pm as the end
        endTime.SetTimeHMS(23, 59, 59);
      }
      endDateTime.SetDateTime(endDate.GetDate(), endTime.GetTime());
      if (endDateTime.IsDateSet())
      {
        if (Tool.DrawingType == DRAWING_RECTANGLE_EXT_HIGHLIGHT)
        {
          // If we have an end date then we don't want to extend the rectangle
          Tool.DrawingType = DRAWING_RECTANGLEHIGHLIGHT;
        }
        Tool.EndDateTime = endDateTime;
      }
      else
      {
        Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize-1];
      }
    }
    else
    {
      Tool.EndDateTime = sc.BaseDateTimeIn[sc.ArraySize-1];
    }

    Tool.Text = tokens.at(6);

    if (colorMap.count(tokens.at(7)) > 0) 
    {
      Tool.Color = colorMap[tokens.at(7)];
      if (price2 > 0) Tool.SecondaryColor = Tool.Color;
    }
    else 
    {
      msg.Format("Unrecognized color name encountered: %s", tokens.at(7));
      sc.AddMessageToLog(msg, false);
      Tool.Color = COLOR_WHITE;
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

    if (tokens.at(9) != "")
    { 
      Tool.LineWidth = atoi(tokens.at(9));
    }

    textalignment = atoi(tokens.at(10));
    // TODO: change to the string compare, but have to figure out how to handle multiple options
    // Actually, as it turns out, you can only do left or right for study-added tools. This can be simplified.
    if (textalignment > 0) Tool.TextAlignment = textalignment;

    // draw line
    Tool.ChartNumber = sc.ChartNumber;
    Tool.AddMethod = UTAM_ADD_OR_ADJUST;
    Tool.ShowPrice = showPrice;
    Tool.TransparencyLevel = transparencyLevel;
    Tool.LineNumber = uniqueLineNumber + inputLineIndex;
    sc.UseTool(Tool);

    // increment row counter
    inputLineIndex++;
  }
}