#include "stdafx.h"
#include "HKCPDisplay.hpp"
#include "Constant.hpp"
#include "EuroScopePlugIn.h"
#include "AT3RadarTargetDisplay.hpp"

using namespace Gdiplus;
using namespace EuroScopePlugIn;

bool AT3RadarTargetDisplay::isRadarEnabled = false;
int AT3RadarTargetDisplay::radarOpacity = 50;
int AT3RadarTargetDisplay::radarDotSize = 2;
bool AT3RadarTargetDisplay::isRadarThreadRunning = false;
std::string AT3RadarTargetDisplay::lastDownloadedUrl = "";
Gdiplus::Bitmap* AT3RadarTargetDisplay::cachedRadarBitmap = nullptr;
std::mutex AT3RadarTargetDisplay::bmpMutex;

AT3RadarTargetDisplay::AT3RadarTargetDisplay(int _CJSLabelSize, int _CJSLabelOffset, bool _CJSLabelShowWhenTracked, double _PlaneIconScale, COLORREF colorA, COLORREF colorNA, COLORREF colorR) :
	CJSLabelSize(_CJSLabelSize), CJSLabelOffset(_CJSLabelOffset), CJSLabelShowWhenTracked(_CJSLabelShowWhenTracked), PlaneIconScale(_PlaneIconScale)
{
	colorAssumed.SetFromCOLORREF(colorA);
	colorNotAssumed.SetFromCOLORREF(colorNA);
	colorRedundant.SetFromCOLORREF(colorR);
}

bool AT3RadarTargetDisplay::OnCompileCommand(const char* sCommandLine, HKCPDisplay* Display) {
	// Check if the command matches exactly (case-insensitive)
	if (strcmp(sCommandLine, ".hkcpwxr") == 0) {

		// Toggle the state
		isRadarEnabled = !isRadarEnabled;

		if (isRadarEnabled) {
			GetPlugIn()->DisplayUserMessage("HKCP", "HKCP", "Weather Radar Enabled", true, true, false, false, false);
			// Only start a new thread if one isn't already running
			if (!isRadarThreadRunning) {
				StartRadarPolling(Display);
			}
		}
		else {
			GetPlugIn()->DisplayUserMessage("HKCP", "HKCP", "Weather Radar Disabled", true, true, false, false, false);
			isRadarThreadRunning = false;
			Display->RefreshMapContent();

			// Safely delete the image from memory
			//std::lock_guard<std::mutex> lock(bmpMutex);
			//if (cachedRadarBitmap != nullptr) {
			//	delete cachedRadarBitmap;
			//	cachedRadarBitmap = nullptr;
			//}
			//lastDownloadedUrl = "";
		}
		return true; // Return true to tell EuroScope we handled this command
	}
	if (strncmp(sCommandLine, ".hkcpwxr opac ", 13) == 0) {

		int inputOpacity = 100;

		// Extract the integer typed after the space
		if (sscanf_s(sCommandLine + 13, "%d", &inputOpacity) == 1) {

			// Clamp the value so users can't type 999 or -50
			if (inputOpacity < 0) inputOpacity = 0;
			if (inputOpacity > 100) inputOpacity = 100;

			radarOpacity = inputOpacity;

			// CRITICAL: Clear the URL cache. 
			// This tricks the background thread into instantly re-processing 
			// and applying the new opacity, instead of making the user wait 
			// for the next HKO weather update!
			lastDownloadedUrl = "";

			char msg[128];
			snprintf(msg, sizeof(msg), "Weather Radar Opacity set to %d%%", radarOpacity);
			GetPlugIn()->DisplayUserMessage("HKCP", "Weather", msg, true, true, false, false, false);
			StartRadarPolling(Display);
			Display->RefreshMapContent();
		}
		return true;
	}

	return false; // Return false if it's not our command, so EuroScope handles it
}

void AT3RadarTargetDisplay::OnRefresh(HDC hDC, int Phase, HKCPDisplay* Display)
{
	if (Phase != REFRESH_PHASE_BEFORE_TAGS) {
		if (Phase == REFRESH_PHASE_BACK_BITMAP) {
			if (isRadarEnabled && cachedRadarBitmap != nullptr) {
				CDC dc;
				dc.Attach(hDC);

				Graphics g(hDC);

				// Lock the mutex so the background thread doesn't delete the bitmap while we draw it
				std::lock_guard<std::mutex> lock(bmpMutex);

				// 1. Define the Lat/Lon for the three required corners
				CPosition posTopLeft, posTopRight, posBottomLeft;

				// INPUT YOUR ACTUAL CORNER COORDINATES HERE
				posTopLeft.LoadFromStrings("E111.40.59.000", "N024.36.20.000");     // Top-Left corner
				posTopRight.LoadFromStrings("E116.39.36.000", "N024.36.20.000");    // Top-Right corner (Example)
				posBottomLeft.LoadFromStrings("E111.40.59.000", "N020.00.03.000");  // Bottom-Left corner (Example)

				// 2. Convert the Lat/Lon into on-screen pixel coordinates
				// As you zoom or pan in EuroScope, these pixel values will automatically update!
				POINT ptTL = Display->ConvertCoordFromPositionToPixel(posTopLeft);
				POINT ptTR = Display->ConvertCoordFromPositionToPixel(posTopRight);
				POINT ptBL = Display->ConvertCoordFromPositionToPixel(posBottomLeft);

				Gdiplus::Point destPoints[3] = {
					Gdiplus::Point(ptTL.x, ptTL.y),
					Gdiplus::Point(ptTR.x, ptTR.y),
					Gdiplus::Point(ptBL.x, ptBL.y)
				};

				g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
				g.DrawImage(cachedRadarBitmap, destPoints, 3);

				//De-allocate graphics objects
				dc.Detach();
				g.ReleaseHDC(hDC);
				dc.DeleteDC();

				return;
			}
			else {
				return;
			}
		}
		return;
	}

	// Create device context
	CDC dc;
	dc.Attach(hDC);

	// Save context for later
	int sDC = dc.SaveDC();

	// Create graphics object
	Graphics g(hDC);

	// Create font
	CFont EuroScopeFont;
	EuroScopeFont.CreateFont(
		CJSLabelSize,              // nHeight
		0,                        // nWidth
		0,                        // nEscapement
		0,                        // nOrientation
		FW_NORMAL,                // nWeight
		FALSE,                    // bItalic
		FALSE,                    // bUnderline
		0,                        // cStrikeOut
		ANSI_CHARSET,             // nCharSet
		OUT_DEFAULT_PRECIS,       // nOutPrecision
		CLIP_DEFAULT_PRECIS,      // nClipPrecision
		DEFAULT_QUALITY,          // nQuality
		DEFAULT_PITCH | FF_SWISS, // nPitchAndFamily
		_T("EuroScope")  // lpszFacename
	);      
	

	// Select first aircraft
	CRadarTarget acft;
	acft = GetPlugIn()->RadarTargetSelectFirst();

	// Loop through all aircrafts
	while (acft.IsValid()) {
		// Get Flight plan and position data
		CFlightPlan fp = Display->GetPlugIn()->FlightPlanSelect(acft.GetCallsign());
		CRadarTargetPositionData pd = acft.GetPosition();

		if (!fp.IsValid() || !pd.IsValid()) {
			acft = GetPlugIn()->RadarTargetSelectNext(acft);
			continue;
		}

		// Skip drawing if not mode C
		if (!pd.GetTransponderC()) {
			acft = GetPlugIn()->RadarTargetSelectNext(acft);
			continue;
		}

		// Setup container
		GraphicsContainer gContainer = g.BeginContainer();

		// Set brush color based on state
		SolidBrush aircraftBrush(colorNotAssumed);
		dc.SetTextColor(colorNotAssumed.ToCOLORREF());
		if (fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED) {
			aircraftBrush.SetColor(colorAssumed);
			dc.SetTextColor(colorAssumed.ToCOLORREF());
		}
		else if (fp.GetState() == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED) {
			aircraftBrush.SetColor(colorAssumed);
			dc.SetTextColor(colorRedundant.ToCOLORREF());
		}
		else if (fp.GetState() == FLIGHT_PLAN_STATE_REDUNDANT || fp.GetState() == FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED) {
			aircraftBrush.SetColor(colorRedundant);
			dc.SetTextColor(colorRedundant.ToCOLORREF());
		}

		// Override aircraft color conditions
		if (pd.GetPressureAltitude() > 100 && strlen(fp.GetTrackingControllerId()) == 0 &&
			fp.GetSectorEntryMinutes() <= 1 && fp.GetSectorEntryMinutes() >= 0) {
			if ((fp.GetDistanceFromOrigin() > 8 && fp.GetDistanceToDestination() > 8) || pd.GetPressureAltitude() > 3000) { //not approaching/departing
				aircraftBrush.SetColor(OVERRIDE_AIW);
			}
		}
		if (strcmp(pd.GetSquawk(), "7700") == 0) {
			aircraftBrush.SetColor(OVERRIDE_EMER);
		}

		// Get and set location
		POINT acftLocation = Display->ConvertCoordFromPositionToPixel(acft.GetPosition().GetPosition());
		g.ScaleTransform(PlaneIconScale, PlaneIconScale, MatrixOrderAppend);
		g.TranslateTransform(acftLocation.x, acftLocation.y, MatrixOrderAppend);
		g.RotateTransform(acft.GetPosition().GetReportedHeadingTrueNorth());

		// Set Anti-aliasing
		g.SetSmoothingMode(SmoothingModeAntiAlias);

		// Define aircraft icon
		Point aircraftIcon[19] = {
			Point(0,-7),
			Point(-1,-6),
			Point(-1,-2),
			Point(-7,3),
			Point(-7,4),
			Point(-1,2),
			Point(-1,6),
			Point(-4,8),
			Point(-4,9),
			Point(0,8),
			Point(4,9),
			Point(4,8),
			Point(1,6),
			Point(1,2),
			Point(7,4),
			Point(7,3),
			Point(1,-2),
			Point(1,-6),
			Point(0,-7)
		};

		// Draw the aircraft icon
		g.FillPolygon(&aircraftBrush, aircraftIcon, 19);

		// Cleanup
		g.EndContainer(gContainer);
		DeleteObject(&aircraftIcon);

		if (fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED && !CJSLabelShowWhenTracked) {
			acft = GetPlugIn()->RadarTargetSelectNext(acft);
			continue;
		}

		// Draw CJS
		dc.SelectObject(EuroScopeFont);
		dc.SetTextAlign(TA_CENTER);
		CSize CJSLabelSize;

		// Set CJS label text to CJS or frequency based on saved state
		string CJSLabelText;
		CJSLabelShowFreq.emplace(fp.GetCallsign(), false);
		if (fp.GetState() == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED) {
			if (CJSLabelShowFreq[fp.GetCallsign()]) {
				CJSLabelText = GetControllerFreqFromId(fp.GetHandoffTargetControllerId());
				dc.SetTextColor(colorAssumed.ToCOLORREF());
			}
			else {
				CJSLabelText = fp.GetHandoffTargetControllerId();
			}
		} else if (fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED) {
			if (CJSLabelShowFreq[fp.GetCallsign()]) {
				CJSLabelText = GetControllerFreqFromId(GetControllerIdFromCallsign(fp.GetCoordinatedNextController()));
				dc.SetTextColor(colorAssumed.ToCOLORREF());
			}
			else {
				CJSLabelText = GetControllerIdFromCallsign(fp.GetCoordinatedNextController());
			}
		} else {
			if (CJSLabelShowFreq[fp.GetCallsign()]) {
				CJSLabelText = GetControllerFreqFromId(fp.GetTrackingControllerId());
				dc.SetTextColor(colorAssumed.ToCOLORREF());
			} else {
				CJSLabelText = fp.GetTrackingControllerId();
			}
		}

		// Remove trailing up to two trailing zeroes
		for (int i = 0; i < 2; i++) {
			if (CJSLabelText.back() == '0') {
				CJSLabelText.pop_back();
			}
		}
		dc.TextOutA(acftLocation.x, acftLocation.y - CJSLabelOffset, CJSLabelText.c_str());

		// Create rectangle around CJS label for click spot
		CJSLabelSize = dc.GetTextExtent(CJSLabelText.c_str());
		POINT CJSLabelPoint = { acftLocation.x - CJSLabelSize.cx / 2, acftLocation.y - CJSLabelOffset};
		CRect CJSLabelRect(CJSLabelPoint, CJSLabelSize);
		Display->AddScreenObject(CJS_INDICATOR, fp.GetCallsign(), CJSLabelRect, true, "");

		// Increment to next aircraft
		acft = GetPlugIn()->RadarTargetSelectNext(acft);
	}

	// Restore context
	dc.RestoreDC(sDC);

	//De-allocate graphics objects
	dc.Detach();
	g.ReleaseHDC(hDC);
	dc.DeleteDC();
}

void AT3RadarTargetDisplay::OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button, HKCPDisplay* Display)
{
	if (ObjectType != CJS_INDICATOR) {
		return;
	}

	if (Button == BUTTON_LEFT) {
		// Toggle between freq and CJS label
		string callsign = sObjectId;
		CJSLabelShowFreq[callsign] = !CJSLabelShowFreq[callsign];
	} else if (Button == BUTTON_RIGHT) {
		// Open next controller menu
		Display->StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_SECTOR_INDICATOR, "", NULL, TAG_ITEM_FUNCTION_ASSIGNED_NEXT_CONTROLLER, Pt, Area);
	}
}

string AT3RadarTargetDisplay::GetControllerFreqFromId(string ID)
{
	double freq = GetPlugIn()->ControllerSelectByPositionId(ID.c_str()).GetPrimaryFrequency();
	if (freq < 100.0) {
		return "";
	}

	string freqString = to_string(freq);
	freqString.resize(7);
	return freqString;
}

string AT3RadarTargetDisplay::GetControllerIdFromCallsign(string callsign)
{
	return GetPlugIn()->ControllerSelect(callsign.c_str()).GetPositionId();
}

void AT3RadarTargetDisplay::StartRadarPolling(HKCPDisplay* Display) {
	isRadarThreadRunning = true;

	std::thread radarThread([this]() {
		while (isRadarThreadRunning) {
			// If the user disabled the radar, exit the thread gracefully
			if (!isRadarEnabled) {
				break;
			}

			time_t curr_time = time(NULL);
			time_t hkt_time = curr_time + (8 * 3600); // UTC+8
			tm* tm_hk = gmtime(&hkt_time);

			int m = tm_hk->tm_min;
			int target_min = 6;

			if (m >= 54) target_min = 54;
			else if (m >= 42) target_min = 42;
			else if (m >= 30) target_min = 30;
			else if (m >= 18) target_min = 18;
			else if (m >= 6) target_min = 6;
			else {
				hkt_time -= 3600;
				tm_hk = gmtime(&hkt_time);
				target_min = 54;
			}

			char url[256];
			snprintf(url, sizeof(url),
				"https://www.hko.gov.hk/wxinfo/radars//radar_256_kml/%04d%02d%02d%02d%02d01_rad_256k.png",
				tm_hk->tm_year + 1900, tm_hk->tm_mon + 1, tm_hk->tm_mday, tm_hk->tm_hour, target_min);

			std::string currentUrl(url);

			// If it's a new URL, download it directly to memory
			if (lastDownloadedUrl != currentUrl && isRadarEnabled) {
				IStream* pStream = nullptr;

				// Download into the IStream
				HRESULT res = URLOpenBlockingStreamA(NULL, url, &pStream, 0, NULL);

				if (res == S_OK && pStream != nullptr && isRadarEnabled) {
					GetPlugIn()->DisplayUserMessage("HKCP", "Weather", "Download OK! Processing image...", true, true, false, false, false);

					Gdiplus::Bitmap* newBmp = new Gdiplus::Bitmap(pStream);
					pStream->Release();

					if (newBmp->GetLastStatus() == Gdiplus::Ok) {

						// 1. Capture the newly generated 1600x1600 upscale bitmap
						// (ApplyMosaicAndRemoveBlack deletes newBmp internally)
						Gdiplus::Bitmap* processedBmp = ApplyMosaicAndRemoveBlack(newBmp, 2);

						if (processedBmp != nullptr && processedBmp->GetLastStatus() == Gdiplus::Ok) {

							// 2. Thread-safely swap the cache pointer
							{
								std::lock_guard<std::mutex> lock(bmpMutex);
								Gdiplus::Bitmap* oldBmp = cachedRadarBitmap;
								cachedRadarBitmap = processedBmp;
								if (oldBmp) delete oldBmp;
							}

							lastDownloadedUrl = currentUrl;
							GetPlugIn()->DisplayUserMessage("HKCP", "Weather", "Image processed.", true, true, false, false, false);
						}
						else {
							if (processedBmp) delete processedBmp;
							GetPlugIn()->DisplayUserMessage("HKCP", "Weather", "Error: Image processing failed.", true, true, false, false, false);
						}
					}
					else {
						delete newBmp;
						GetPlugIn()->DisplayUserMessage("HKCP", "Weather", "Error: Corrupted image received.", true, true, false, false, false);
					}
				}
			}
		}

		// Ensure flag is reset when thread dies naturally
		isRadarThreadRunning = false;
		});
	Display->RefreshMapContent();
	radarThread.detach();
}

Gdiplus::Bitmap* AT3RadarTargetDisplay::ApplyMosaicAndRemoveBlack(Gdiplus::Bitmap* srcBmp, int mosaicSize) {
	if (!srcBmp) return nullptr;

	// Use the dynamic dot size! (1 = solid blocks, 2 = standard dots, 4 = tiny dots)
	int upscale = radarDotSize;

	UINT hiWidth = srcBmp->GetWidth() * upscale;
	UINT hiHeight = srcBmp->GetHeight() * upscale;

	Gdiplus::Bitmap* hiResBmp = new Gdiplus::Bitmap(hiWidth, hiHeight, PixelFormat32bppARGB);
	{
		Gdiplus::Graphics g(hiResBmp);
		g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		g.DrawImage(srcBmp, 0, 0, hiWidth, hiHeight);
	}

	Gdiplus::Rect rect(0, 0, hiWidth, hiHeight);
	Gdiplus::BitmapData bmpData;
	hiResBmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

	int stride = bmpData.Stride;
	UINT8* pixels = (UINT8*)bmpData.Scan0;

	// Scale the mosaic blocks to match the new 1600x1600 grid
	int scaledMosaic = mosaicSize * upscale;

	for (UINT y = 0; y < hiHeight; y += scaledMosaic) {
		for (UINT x = 0; x < hiWidth; x += scaledMosaic) {

			int sampleIdx = y * stride + x * 4;

			UINT8 b = pixels[sampleIdx];
			UINT8 g = pixels[sampleIdx + 1];
			UINT8 r = pixels[sampleIdx + 2];
			UINT8 a = pixels[sampleIdx + 3];

			bool isBlack = (r < 10 && g < 10 && b < 10);
			bool isGrayscale = (abs(r - g) < 20 && abs(g - b) < 20);

			UINT8 snapR = 0, snapG = 0, snapB = 0;
			bool isVisible = !(isBlack || isGrayscale || a < 10);

			if (isVisible) {
				if (r > 100 && g < 80) {
					snapR = 194; snapG = 41; snapB = 30;
				}
				else if (r > 80 && g >= 80) {
					snapR = 240; snapG = 200; snapB = 0;
				}
				else {
					snapR = 78; snapG = 148; snapB = 40;
				}
			}

			for (UINT by = 0; by < (UINT)scaledMosaic && (y + by) < hiHeight; by++) {
				for (UINT bx = 0; bx < (UINT)scaledMosaic && (x + bx) < hiWidth; bx++) {

					int pixelIdx = (y + by) * stride + (x + bx) * 4;

					UINT pixelX = x + bx;
					UINT pixelY = y + by;

					// Dot noise pattern evaluated at the finer 1600x1600 resolution
					bool isDot = (((pixelX * 3241) + (pixelY * 5479)) % 100) < 35;

					if (!isVisible || !isDot) {
						pixels[pixelIdx + 3] = 0;
					}
					else {
						pixels[pixelIdx] = snapB;
						pixels[pixelIdx + 1] = snapG;
						pixels[pixelIdx + 2] = snapR;

						float opacityMultiplier = radarOpacity / 100.0f;
						pixels[pixelIdx + 3] = (UINT8)(255 * opacityMultiplier);
					}
				}
			}
		}
	}

	hiResBmp->UnlockBits(&bmpData);

	// Free the original 800x800 bitmap from memory
	delete srcBmp;
	return hiResBmp;
}