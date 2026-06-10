// raspivoice
// Based on:
// http://www.seeingwithsound.com/hificode_OpenCV.cpp
// C program for soundscape generation. (C) P.B.L. Meijer 1996
// hificode.c modified for camera input using OpenCV. (C) 2013
// Last update: December 29, 2014; released under the Creative
// Commons Attribution 4.0 International License (CC BY 4.0),
// see http://www.seeingwithsound.com/im2sound.htm for details
// License: https://creativecommons.org/licenses/by/4.0/

#include <iostream>
#include <cstdlib>
#include <chrono>
#include <unistd.h>
#include "RaspiVoice.h"
#include "ImageToSoundscape.h"
#include "test_image.h"
#include "printtime.h"

RaspiVoice::RaspiVoice(RaspiVoiceOptions opt) :
	rows(opt.rows),
	columns(opt.columns),
	image_source(opt.image_source),
	preview(opt.preview),
	use_bw_test_image(opt.use_bw_test_image),
	verbose(opt.verbose),
	opt(opt)
{
	if ((image_source == 0) && (opt.input_filename == "")) //Test image, fixed size
	{
		rows = 64;
		columns = 64;
	}
	init();

	i2ssConverter = new ImageToSoundscapeConverter(rows, columns, opt.freq_lowest, opt.freq_highest, opt.sample_freq_Hz, opt.total_time_s, opt.use_exponential, opt.use_stereo, opt.use_delay, opt.use_fade, opt.use_diffraction, opt.use_bspline, opt.speed_of_sound_m_s, opt.acoustical_size_of_head_m);
}

RaspiVoice::~RaspiVoice()
{
	if (i2ssConverter)
	{
		delete(i2ssConverter);
	}

#ifndef NO_RASPICAM
	if (image_source == 1)
	{
		raspiCam.release();
	}
#endif
	if (image_source >= 2)
	{
		captureRunning_ = false;
		pthread_join(captureThread_, nullptr);
		pthread_mutex_destroy(&rawFrameMutex_);
	}
}

void* RaspiVoice::captureLoop(void* self)
{
	RaspiVoice* rv = static_cast<RaspiVoice*>(self);
	while (rv->captureRunning_)
	{
		cv::Mat frame;
		rv->cap.read(frame);
		if (!frame.empty())
		{
			pthread_mutex_lock(&rv->rawFrameMutex_);
			rv->latestRawFrame_ = frame.clone();
			pthread_mutex_unlock(&rv->rawFrameMutex_);
		}
	}
	return nullptr;
}


void RaspiVoice::init()
{
	if (verbose)
	{
		printtime("");
		printtime("Init...");
	}

	image = new std::vector<float>(rows*columns);

	if (image_source == 0) //Test image
	{
		if (opt.input_filename == "")
		{
			initTestImage();
		}
		else
		{
			initFileImage();
		}
		
	}
	else if (image_source == 1) //RaspiCAM
	{
#ifndef NO_RASPICAM
		initRaspiCam();
#else
		throw(std::runtime_error("RaspiCam (image_source=1) not compiled in. Use -s2 for USB camera."));
#endif
	}
	else if (image_source >= 2)
	{
		initUsbCam();
	}

	if (preview)
	{
		cv::namedWindow("RaspiVoice Preview", cv::WINDOW_NORMAL);
		// Side-by-side: raw | vOICe. Double wide.
		// Override width with `PREVIEW_WIDTH=1920 ./run.sh -p`.
		cv::resizeWindow("RaspiVoice Preview", 1920 * 2, 1080);
	}

	//Test read + process one image:
	cv::Mat im = readImage();
	processImage(im);
}

void RaspiVoice::initTestImage()
{
	if (verbose)
	{
		if (use_bw_test_image)
		{
			std::cout << "Using B/W test image" << std::endl;
		}
		else
		{
			std::cout << "Using grayscale test image" << std::endl;
		}
	}

	// Set hard-coded image
	for (int j = 0; j < columns; j++)
	{
		for (int i = 0; i < rows; i++)
		{
			if (use_bw_test_image)
			{
				if (P_bw[rows - i - 1][j] != '#')
				{
					(*image)[IDX2D(i, j)] = 1.0; // white;
				}
				else
				{
					(*image)[IDX2D(i, j)] = 0.0; // black;
				}
			}
			else if (P_grayscale[i][j] > 'a')
			{
				(*image)[IDX2D(rows - i - 1, j)] = pow(10.0, (P_grayscale[i][j] - 'a' - 15) / 10.0);   // 2dB steps
			}
			else
			{
				(*image)[IDX2D(rows - i - 1, j)] = 0.0;
			}
		}
	}
}

void RaspiVoice::initFileImage()
{
	if (verbose)
	{
		std::cout << "Checking input file " << opt.input_filename << std::endl;

		//Test read:
		cv::Mat mat = cv::imread(opt.input_filename.c_str(), cv::IMREAD_GRAYSCALE);

		if (mat.data == NULL)
		{
			throw(std::runtime_error("Cannot read input file as image."));
		}

		std::cout << "ok" << std::endl;
	}
}


#ifndef NO_RASPICAM
void RaspiVoice::initRaspiCam()
{
	if (verbose)
	{
		std::cout << "Opening RaspiCam..." << std::endl;
	}

	raspiCam.set(cv::CAP_PROP_FORMAT, CV_8UC1);
	raspiCam.set(cv::CAP_PROP_FRAME_WIDTH, 320); //Somehow, other resolutions did not work
	raspiCam.set(cv::CAP_PROP_FRAME_HEIGHT, 240);
	if ((opt.exposure >= 1) && (opt.exposure <= 100))
	{
		raspiCam.set(cv::CAP_PROP_EXPOSURE, opt.exposure);
	}

	if (!raspiCam.open())
	{
		throw(std::runtime_error("Error opening RaspiCam."));
	}
	else
	{
		if (verbose)
		{
			std::cout << "Ok" << std::endl;
		}
	}
}
#endif

void RaspiVoice::initUsbCam()
{
	int cam_id = image_source - 2;  // OpenCV camera index 0+

	if (verbose)
	{
		std::cout << "Opening USB camera..." << std::endl;
	}

	// Force V4L2 backend + MJPG FOURCC — required by the IR camera (matches ir-cam.py).
	cap.open(cam_id, cv::CAP_V4L2);

	if (!cap.isOpened())
	{
		throw(std::runtime_error("Could not open camera."));
	}
	cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
	// Setting standard capture size, may fail; resize later
	cv::Mat rawImage;
	cap.read(rawImage);  // Dummy read needed with some devices
	cap.set(cv::CAP_PROP_FRAME_WIDTH, 320); //Increased resolution necessary for foveal mapping
	cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);
	if ((opt.exposure >= 1) && (opt.exposure <= 100))
	{
		// v4l2-ctl is more reliable than CAP_PROP_AUTO_EXPOSURE for disabling
		// auto-exposure on USB cameras — OpenCV's setter is often silently ignored.
		char v4l2cmd[128];
		snprintf(v4l2cmd, sizeof(v4l2cmd),
			"v4l2-ctl -d /dev/video%d --set-ctrl=auto_exposure=1 --set-ctrl=exposure_time_absolute=%d 2>/dev/null",
			cam_id, opt.exposure * 10);
		system(v4l2cmd);
	}

	// Start background thread that keeps latestRawFrame_ fresh at camera FPS.
	pthread_mutex_init(&rawFrameMutex_, nullptr);
	captureRunning_ = true;
	pthread_create(&captureThread_, nullptr, captureLoop, this);

	if (verbose)
	{
		std::cout << "Ok" << std::endl;
	}
}

cv::Mat RaspiVoice::readImage()
{
	cv::Mat rawImage;
	cv::Mat processedImage;

	if (verbose)
	{
		printtime("ReadImage start");
	}
	if ((image_source == 0) && (opt.input_filename != ""))
	{
		rawImage = cv::imread(opt.input_filename.c_str(), cv::IMREAD_GRAYSCALE);
		processedImage = rawImage;
	}
#ifndef NO_RASPICAM
	else if (image_source == 1) //RaspiCAM
	{
		raspiCam.grab();
		raspiCam.retrieve(rawImage);
		if (opt.read_frames > 1)
		{
			for (int r = 1; r < opt.read_frames; r++)
			{
				raspiCam.grab();
				raspiCam.retrieve(rawImage);
			}
		}
		processedImage = rawImage;
	}
#endif
	else if (image_source >= 2) //OpenCv camera — use background thread's latest frame
	{
		// Wait up to 2s for the capture thread to deliver the first frame.
		for (int tries = 0; tries < 200; tries++)
		{
			pthread_mutex_lock(&rawFrameMutex_);
			rawImage = latestRawFrame_.clone();
			pthread_mutex_unlock(&rawFrameMutex_);
			if (!rawImage.empty()) break;
			usleep(10000); // 10ms
		}

		if (rawImage.empty())
		{
			throw(std::runtime_error("Error reading frame from camera."));
		}

		if (rawImage.channels() == 1)
			processedImage = rawImage;
		else
			cv::cvtColor(rawImage, processedImage, cv::COLOR_BGR2GRAY);
	}

	return processedImage;
}


void RaspiVoice::processImage(cv::Mat rawImage)
{
	cv::Mat processedImage = rawImage;

	if (verbose)
	{
		printtime("ProcessImage start");
	}

	if ((image_source > 0) || (opt.input_filename != ""))
	{
		if (opt.foveal_mapping)
		{
			cv::Matx33f cameraMatrix(100, 0, processedImage.cols / 2, 0, 100, processedImage.rows / 2, 0, 0, 1);
			cv::Matx41f distCoeffs(5.0, 5.0, 0, 0);
			cv::Mat processedImage2;
			cv::undistort(processedImage, processedImage2, cameraMatrix, distCoeffs);
			float clipzoom = 1.8; //horizontal zoom to remove blinders, decreases resolution if > 1.0
			cv::Rect roi(processedImage.cols / 2 - columns / 2 / clipzoom, processedImage.rows / 2 - rows / 2, columns / clipzoom, rows);
			processedImage = processedImage2(roi);
		}

		if (opt.zoom > 1.0)
		{
			int w = processedImage.cols;
			int h = processedImage.rows;
			float z = opt.zoom;
			cv::Rect roi((w / 2.0) - w / (2.0*z), (h / 2.0) - h / (2.0*z), w/z, h/z);
			processedImage = processedImage(roi);
		}

		//Bring to size needed by ImageToSoundscape:
		if (processedImage.rows != rows || processedImage.cols != columns)
		{
			cv::resize(processedImage, processedImage, cv::Size(columns, rows));
		}

		if ((opt.blinders > 0) && (opt.blinders < columns/2))
		{
			processedImage(cv::Rect(0, 0, opt.blinders, rows - 1)).setTo(0);
			processedImage(cv::Rect(columns - 1 - opt.blinders, 0, opt.blinders, rows - 1)).setTo(0);
		}

		if ((opt.contrast != 1.0) || (opt.brightness != 0))
		{
			float alpha = opt.contrast;
			int beta = opt.brightness;

			for (int y = 0; y < processedImage.rows; y++)
			{
				for (int x = 0; x < processedImage.cols; x++)
				{
					processedImage.at<uchar>(y, x) = cv::saturate_cast<uchar>(alpha*(processedImage.at<uchar>(y, x)) + beta);
				}
			}
		}

		if (opt.threshold > 0)
		{
			if (opt.threshold < 255)
			{
				cv::threshold(processedImage, processedImage, opt.threshold, 255, cv::THRESH_BINARY);
			}
			else
			{
				//Auto threshold:
				cv::threshold(processedImage, processedImage, 127, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
			}
		}

		if (opt.negative_image)
		{
			cv::Mat sub_mat = cv::Mat::ones(processedImage.size(), processedImage.type()) * 255;
			cv::subtract(sub_mat, processedImage, processedImage);
		}

		if (opt.edge_detection_opacity > 0.0)
		{
			cv::Mat blurImage;
			cv::Mat edgesImage;
			int ratio = 3;
			int kernel_size = 3;
			int lowThreshold = opt.edge_detection_threshold;
			if (lowThreshold <= 0)
			{
				lowThreshold = 127;
			}
			cv::blur(processedImage, blurImage, cv::Size(3, 3));
			cv::Canny(blurImage, edgesImage, lowThreshold, lowThreshold*ratio, kernel_size);

			double alpha = opt.edge_detection_opacity;
			if (alpha > 1.0)
			{
				alpha = 1.0;
			}
			double beta = (1.0 - alpha);
			cv::addWeighted(edgesImage, alpha, processedImage, beta, 0.0, processedImage);
		}

		if ((opt.flip >= 1) && (opt.flip <= 3))
		{
			int flipCode;
			if (opt.flip == 1) //h
			{
				flipCode = 1;
			}
			else if (opt.flip == 2) //v
			{
				flipCode = 0;
			}
			else if (opt.flip == 3) //h+v
			{
				flipCode = -1;
			}

			cv::flip(processedImage, processedImage, flipCode);
		}

		if (preview)
		{
			const char* env_w = std::getenv("PREVIEW_WIDTH");
			int target_w = (env_w && std::atoi(env_w) > 0) ? std::atoi(env_w) : 1920;
			int target_h = (target_w * processedImage.rows) / processedImage.cols;

			// Left panel: raw camera frame (grayscale for fair comparison)
			cv::Mat rawGray, rawPanel, processedPanel;
			if (rawImage.channels() == 1)
				rawGray = rawImage;
			else
				cv::cvtColor(rawImage, rawGray, cv::COLOR_BGR2GRAY);
			cv::resize(rawGray, rawPanel, cv::Size(target_w, target_h), 0, 0, cv::INTER_LINEAR);

			// Right panel: vOICe algorithm input (nearest-neighbor keeps pixels honest)
			cv::resize(processedImage, processedPanel, cv::Size(target_w, target_h), 0, 0, cv::INTER_NEAREST);

			// Stack side by side and label
			cv::Mat combined;
			cv::hconcat(rawPanel, processedPanel, combined);
			cv::putText(combined, "Raw IR", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255), 2);
			cv::putText(combined, "vOICe input", cv::Point(target_w + 20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255), 2);

			lastPreviewFrame_ = combined;
			cv::imshow("RaspiVoice Preview", combined);
			cv::waitKey(1);
		}

		/* Set live camera image */
		for (int j = 0; j < columns; j++)
		{
			for (int i = 0; i < rows; i++)
			{
				int mVal = processedImage.at<uchar>(rows - 1 - i, j) / 16;
				if (mVal == 0)
				{
					(*image)[IDX2D(i, j)] = 0;
				}
				else
				{
					(*image)[IDX2D(i, j)] = pow(10.0, (mVal - 15) / 10.0);   // 2dB steps
				}
			}
		}
	}

}

void RaspiVoice::GrabAndProcessFrame(RaspiVoiceOptions opt)
{
	//Set new options. Options that have been copied to RaspiVoice:: fields in constructor are unaffected.
	this->opt = opt;

	//Read and process images:
	cv::Mat im = readImage();
	processImage(im);

	if (verbose)
	{
		printtime("vOICe algorithm process start");
	}
	i2ssConverter->Process(*image);
}

void RaspiVoice::PlayFrame(RaspiVoiceOptions opt)
{
	if (opt.quit)
	{
		return;
	}

	if (!opt.mute)
	{
		AudioData &audioData = i2ssConverter->GetAudioData();
		audioData.CardNumber = opt.audio_card;
		audioData.Verbose = verbose;

		if (verbose)
		{
			printtime("Playing audio");
		}

		audioData.Play();

		// While audio plays in the background, keep refreshing the preview
		// so the raw IR panel updates at camera FPS instead of ~1fps.
		if (preview && !lastPreviewFrame_.empty())
		{
			int duration_ms = audioData.getDurationMs();
			auto t0 = std::chrono::steady_clock::now();
			while (true)
			{
				// Build fresh raw panel from background thread
				pthread_mutex_lock(&rawFrameMutex_);
				cv::Mat raw = latestRawFrame_.clone();
				pthread_mutex_unlock(&rawFrameMutex_);

				if (!raw.empty())
				{
					const char* env_w = std::getenv("PREVIEW_WIDTH");
					int target_w = (env_w && std::atoi(env_w) > 0) ? std::atoi(env_w) : 1920;
					int target_h = (target_w * lastPreviewFrame_.rows) / (lastPreviewFrame_.cols / 2);
					cv::Mat rawGray, rawPanel;
					if (raw.channels() == 1) rawGray = raw;
					else cv::cvtColor(raw, rawGray, cv::COLOR_BGR2GRAY);
					cv::resize(rawGray, rawPanel, cv::Size(target_w, target_h), 0, 0, cv::INTER_LINEAR);

					// Replace left half of lastPreviewFrame_ with fresh raw
					cv::Mat right = lastPreviewFrame_(cv::Rect(lastPreviewFrame_.cols / 2, 0,
					                                           lastPreviewFrame_.cols / 2, lastPreviewFrame_.rows));
					cv::Mat combined;
					cv::hconcat(rawPanel, right, combined);
					cv::putText(combined, "Raw IR", cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255), 2);
					cv::putText(combined, "vOICe input", cv::Point(target_w + 20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255), 2);
					cv::imshow("RaspiVoice Preview", combined);
				}

				cv::waitKey(1);
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				    std::chrono::steady_clock::now() - t0).count();
				if (elapsed >= duration_ms) break;
			}
		}

		if (opt.output_filename != "")
		{
			audioData.SaveToWavFile(opt.output_filename);
		}

		//audioData.PlayWav(FNAME);
	}
	else if (verbose)
	{
		printtime("Muted, not playing audio");
	}
}


