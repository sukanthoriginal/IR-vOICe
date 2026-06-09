#pragma once

#include <vector>
#ifndef NO_RASPICAM
#include <raspicam/raspicam_cv.h>
#endif
#include <opencv2/opencv.hpp>

#include "Options.h"
#include "ImageToSoundscape.h"

class RaspiVoice
{
private:
	int rows;
	int columns;
	int image_source;
	bool preview;
	bool use_bw_test_image;
	bool verbose;
	RaspiVoiceOptions opt;

	ImageToSoundscapeConverter *i2ssConverter;
#ifndef NO_RASPICAM
	raspicam::RaspiCam_Cv raspiCam;
#endif
	cv::VideoCapture cap;
	std::vector<float> *image;

	RaspiVoice(const RaspiVoice& other) = delete;
	RaspiVoice& operator=(const RaspiVoice&) = delete;

	void init();
	void initFileImage();
	void initTestImage();
#ifndef NO_RASPICAM
	void initRaspiCam();
#endif
	void initUsbCam();
	cv::Mat readImage();
	void processImage(cv::Mat rawImage);
	int playWav(std::string filename);
public:
	RaspiVoice(RaspiVoiceOptions opt);
	~RaspiVoice();
	void GrabAndProcessFrame(RaspiVoiceOptions opt);
	void PlayFrame(RaspiVoiceOptions opt);
};

