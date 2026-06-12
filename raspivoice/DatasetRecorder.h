#pragma once

#include <string>
#include <deque>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <pthread.h>

#include <opencv2/core.hpp>

// Per-frame paired-file recorder: writes raw_NNNNNN.png + voice_NNNNNN.png +
// audio_NNNNNN.wav into DIR/session_YYYYMMDD_HHMMSS/ and appends a row to
// metadata.csv. Disk I/O runs on a background thread; if the queue fills
// (writer can't keep up), oldest frames are dropped so the audio pipeline
// never blocks.
class DatasetRecorder
{
public:
	explicit DatasetRecorder(const std::string& base_dir);
	~DatasetRecorder();

	bool IsReady() const { return ready_; }
	const std::string& SessionDir() const { return session_dir_; }

	// Enqueue a frame for asynchronous write. Mats are cloned, samples copied.
	void Enqueue(const cv::Mat& raw_ir,
	             const cv::Mat& voice_input,
	             const uint16_t* samples,
	             int sample_count,
	             int sample_freq_Hz,
	             bool stereo,
	             int exposure,
	             double mean_brightness);

private:
	struct FrameItem
	{
		int frame_num;
		std::string iso_timestamp;
		int exposure;
		double mean_brightness;
		cv::Mat raw_ir;
		cv::Mat voice_input;
		std::vector<uint16_t> samples;
		int sample_freq_Hz;
		bool stereo;
	};

	static void* writerEntry(void* self);
	void writerLoop();
	void writeFrame(const FrameItem& f);
	void writeWav(const std::string& path, const FrameItem& f);

	std::string session_dir_;
	FILE* csv_;
	pthread_mutex_t csv_mutex_;

	std::deque<FrameItem> queue_;
	pthread_mutex_t queue_mutex_;
	pthread_cond_t queue_cv_;
	pthread_t writer_thread_;
	volatile bool stop_;
	bool ready_;
	int frame_counter_;
	int dropped_;
	size_t max_queue_;
};
