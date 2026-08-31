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

	string GetControllerFreqFromId(string ID);

	string GetControllerIdFromCallsign(string callsign);

	void StartRadarPolling(HKCPDisplay* Display);

	void ApplyMosaicAndRemoveBlack(Gdiplus::Bitmap* bmp, int mosaicSize);

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
	static bool isRadarThreadRunning;
	static std::string lastDownloadedUrl;
	static Gdiplus::Bitmap* cachedRadarBitmap;
	static std::mutex bmpMutex;

	Color colorAssumed;
	Color colorNotAssumed;
	Color colorRedundant;
};

