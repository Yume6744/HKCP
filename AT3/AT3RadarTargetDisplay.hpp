#pragma once
#include "stdafx.h"
#include "EuroScopePlugIn.h"
#include "HKCPDisplay.hpp"
#include <sstream>
#include <vector>
#include <string>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <gdiplus.h>
#include "AT3Tags.hpp"


using namespace std;
using namespace EuroScopePlugIn;
using namespace Gdiplus;

class HKCPDisplay;
class HKCPPlugin;

class AT3RadarTargetDisplay :
    public EuroScopePlugIn::CRadarScreen
{
public:
    AT3RadarTargetDisplay(int _CJSLabelSize, int _CJSLabelOffset, bool _CJSLabelShowWhenTracked, double _PlaneIconScale, COLORREF colorA, COLORREF colorNA, COLORREF colorR);
    
	virtual bool OnCompileCommand(const char* sCommandLine, HKCPDisplay* Display);

    void OnRefresh(HDC hDC, int Phase, HKCPDisplay* Display);

	void OnClickScreenObject(int ObjectType,
		const char* sObjectId,
		POINT Pt,
		RECT Area,
		int Button,
		HKCPDisplay* Display);

	void normalRouteDraw(CFlightPlan fp);

	string GetControllerFreqFromId(string ID);

	string GetControllerIdFromCallsign(string callsign);

	void StartRadarPolling(HKCPDisplay* Display);

	Gdiplus::Bitmap* ApplyMosaicAndRemoveBlack(Gdiplus::Bitmap* srcBmp, int mosaicSize);

	//  This gets called before OnAsrContentToBeSaved()
	inline virtual void OnAsrContentToBeClosed(void)
	{
		delete this;
	};
private:
	int CJSLabelSize;
	int CJSLabelOffset;
	bool CJSLabelShowWhenTracked;
	double PlaneIconScale;
	unordered_map<string, bool> CJSLabelShowFreq;
	static bool isRadarEnabled;
	static int radarOpacity;
	static int radarDotSize;
	static bool isRadarThreadRunning;
	static std::string lastDownloadedUrl;
	static Gdiplus::Bitmap* cachedRadarBitmap;
	static std::mutex bmpMutex;

	Color colorAssumed;
	Color colorNotAssumed;
	Color colorRedundant;
	Color colorRouteDraw;
	Color colorRouteDrawDCT;

	string formatRouteTag(CFlightPlanExtractedRoute extractedRoute, int nextPointID, tm* tm_gmt);

	void createRouteDraw(CFlightPlan fp, POINT acftLocation, int drawType, int nextPointID, int probeNextID, Graphics* g, CDC* dc, HKCPDisplay* Display);
};

