#include "DatasetRecorder.h"

#include <ctime>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>

#include <opencv2/imgcodecs.hpp>

namespace {

std::string nowIsoUtc()
{
	std::time_t t = std::time(nullptr);
	std::tm tmv;
	gmtime_r(&t, &tmv);
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
	return std::string(buf);
}

std::string nowStampLocal()
{
	std::time_t t = std::time(nullptr);
	std::tm tmv;
	localtime_r(&t, &tmv);
	char buf[32];
	strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmv);
	return std::string(buf);
}

bool mkdirRecursive(const std::string& path)
{
	if (path.empty()) return false;
	std::string acc;
	for (size_t i = 0; i < path.size(); ++i)
	{
		char c = path[i];
		acc.push_back(c);
		if (c == '/' || i == path.size() - 1)
		{
			if (acc == "/" || acc.empty()) continue;
			if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST)
				return false;
		}
	}
	return true;
}

} // anon

DatasetRecorder::DatasetRecorder(const std::string& base_dir) :
	csv_(nullptr),
	stop_(false),
	ready_(false),
	frame_counter_(0),
	dropped_(0),
	max_queue_(8)
{
	pthread_mutex_init(&queue_mutex_, nullptr);
	pthread_cond_init(&queue_cv_, nullptr);
	pthread_mutex_init(&csv_mutex_, nullptr);

	if (!mkdirRecursive(base_dir))
	{
		std::cerr << "DatasetRecorder: cannot create base dir " << base_dir
		          << " (" << strerror(errno) << ")" << std::endl;
		return;
	}

	session_dir_ = base_dir + "/session_" + nowStampLocal();
	if (!mkdirRecursive(session_dir_))
	{
		std::cerr << "DatasetRecorder: cannot create session dir " << session_dir_
		          << " (" << strerror(errno) << ")" << std::endl;
		return;
	}

	std::string csv_path = session_dir_ + "/metadata.csv";
	csv_ = fopen(csv_path.c_str(), "w");
	if (!csv_)
	{
		std::cerr << "DatasetRecorder: cannot open " << csv_path
		          << " (" << strerror(errno) << ")" << std::endl;
		return;
	}
	fprintf(csv_, "frame,timestamp_iso,exposure,mean_brightness\n");
	fflush(csv_);

	ready_ = true;
	pthread_create(&writer_thread_, nullptr, writerEntry, this);
	std::cout << "DatasetRecorder: writing to " << session_dir_ << std::endl;
}

DatasetRecorder::~DatasetRecorder()
{
	pthread_mutex_lock(&queue_mutex_);
	stop_ = true;
	pthread_cond_broadcast(&queue_cv_);
	pthread_mutex_unlock(&queue_mutex_);

	if (ready_)
		pthread_join(writer_thread_, nullptr);

	if (csv_)
	{
		fclose(csv_);
		csv_ = nullptr;
	}
	pthread_mutex_destroy(&queue_mutex_);
	pthread_cond_destroy(&queue_cv_);
	pthread_mutex_destroy(&csv_mutex_);

	if (dropped_ > 0)
		std::cerr << "DatasetRecorder: dropped " << dropped_
		          << " frame(s) (writer fell behind)" << std::endl;
}

void DatasetRecorder::Enqueue(const cv::Mat& raw_ir,
                              const cv::Mat& voice_input,
                              const uint16_t* samples,
                              int sample_count,
                              int sample_freq_Hz,
                              bool stereo,
                              int exposure,
                              double mean_brightness)
{
	if (!ready_) return;

	FrameItem item;
	item.frame_num = ++frame_counter_;
	item.iso_timestamp = nowIsoUtc();
	item.exposure = exposure;
	item.mean_brightness = mean_brightness;
	item.raw_ir = raw_ir.clone();
	item.voice_input = voice_input.clone();
	item.sample_freq_Hz = sample_freq_Hz;
	item.stereo = stereo;
	size_t total = static_cast<size_t>(sample_count) * (stereo ? 2 : 1);
	item.samples.assign(samples, samples + total);

	pthread_mutex_lock(&queue_mutex_);
	while (queue_.size() >= max_queue_)
	{
		queue_.pop_front();
		++dropped_;
	}
	queue_.push_back(std::move(item));
	pthread_cond_signal(&queue_cv_);
	pthread_mutex_unlock(&queue_mutex_);
}

void* DatasetRecorder::writerEntry(void* self)
{
	static_cast<DatasetRecorder*>(self)->writerLoop();
	return nullptr;
}

void DatasetRecorder::writerLoop()
{
	while (true)
	{
		pthread_mutex_lock(&queue_mutex_);
		while (!stop_ && queue_.empty())
			pthread_cond_wait(&queue_cv_, &queue_mutex_);

		if (stop_ && queue_.empty())
		{
			pthread_mutex_unlock(&queue_mutex_);
			return;
		}

		FrameItem item = std::move(queue_.front());
		queue_.pop_front();
		pthread_mutex_unlock(&queue_mutex_);

		writeFrame(item);
	}
}

void DatasetRecorder::writeFrame(const FrameItem& f)
{
	char stem[32];
	snprintf(stem, sizeof(stem), "%06d", f.frame_num);

	std::string raw_path   = session_dir_ + "/raw_"   + stem + ".png";
	std::string voice_path = session_dir_ + "/voice_" + stem + ".png";
	std::string wav_path   = session_dir_ + "/audio_" + stem + ".wav";

	if (!cv::imwrite(raw_path, f.raw_ir))
		std::cerr << "DatasetRecorder: failed to write " << raw_path << std::endl;
	if (!cv::imwrite(voice_path, f.voice_input))
		std::cerr << "DatasetRecorder: failed to write " << voice_path << std::endl;
	writeWav(wav_path, f);

	pthread_mutex_lock(&csv_mutex_);
	if (csv_)
	{
		fprintf(csv_, "%d,%s,%d,%.3f\n",
		        f.frame_num,
		        f.iso_timestamp.c_str(),
		        f.exposure,
		        f.mean_brightness);
		fflush(csv_);
	}
	pthread_mutex_unlock(&csv_mutex_);
}

static void put_u16le(FILE* fp, uint16_t v)
{
	fputc(v & 0xFF, fp);
	fputc((v >> 8) & 0xFF, fp);
}

static void put_u32le(FILE* fp, uint32_t v)
{
	fputc(v & 0xFF, fp);
	fputc((v >> 8) & 0xFF, fp);
	fputc((v >> 16) & 0xFF, fp);
	fputc((v >> 24) & 0xFF, fp);
}

void DatasetRecorder::writeWav(const std::string& path, const FrameItem& f)
{
	FILE* fp = fopen(path.c_str(), "wb");
	if (!fp)
	{
		std::cerr << "DatasetRecorder: cannot open " << path
		          << " (" << strerror(errno) << ")" << std::endl;
		return;
	}
	const int channels = f.stereo ? 2 : 1;
	const int bytes_per_sample = channels * 2;
	const uint32_t frames = static_cast<uint32_t>(f.samples.size() / channels);
	const uint32_t data_bytes = frames * bytes_per_sample;

	fputs("RIFF", fp);
	put_u32le(fp, data_bytes + 36);
	fputs("WAVEfmt ", fp);
	put_u32le(fp, 16);
	put_u16le(fp, 1);
	put_u16le(fp, channels);
	put_u32le(fp, f.sample_freq_Hz);
	put_u32le(fp, f.sample_freq_Hz * bytes_per_sample);
	put_u16le(fp, bytes_per_sample);
	put_u16le(fp, 16);
	fputs("data", fp);
	put_u32le(fp, data_bytes);
	fwrite(f.samples.data(), sizeof(uint16_t), f.samples.size(), fp);
	fclose(fp);
}
