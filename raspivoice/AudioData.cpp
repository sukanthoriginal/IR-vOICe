// raspivoice
// Based on:
// http://www.seeingwithsound.com/hificode_OpenCV.cpp
// C program for soundscape generation. (C) P.B.L. Meijer 1996
// hificode.c modified for camera input using OpenCV. (C) 2013
// Last update: December 29, 2014; released under the Creative
// Commons Attribution 4.0 International License (CC BY 4.0),
// see http://www.seeingwithsound.com/im2sound.htm for details
// License: https://creativecommons.org/licenses/by/4.0/

#include <cstring>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

#include "AudioData.h"

pthread_mutex_t AudioData::audio_mutex;

AudioData::AudioData(int card_number, int sample_freq_Hz, int sample_count, bool use_stereo) :
	sample_freq_Hz(sample_freq_Hz),
	sample_count(sample_count),
	use_stereo(use_stereo),
	Verbose(false),
	CardNumber(card_number),
	samplebuffer(std::vector<uint16_t>((use_stereo ? 2 : 1) * sample_count)),
	volume(-1),
	newvolume(-1),
	aplay_pipe_(nullptr),
	pipe_card_(-1)
{
}

AudioData::~AudioData()
{
	closePipe();
}

void AudioData::Init()
{
	pthread_mutex_init(&audio_mutex, NULL);
	// A dead aplay must not kill us with SIGPIPE on fwrite().
	signal(SIGPIPE, SIG_IGN);
}

void AudioData::ensurePipeOpen()
{
	if (aplay_pipe_ != nullptr && pipe_card_ == CardNumber) return;
	closePipe();
	if (sample_count <= 0) return;

	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"aplay -q -t raw -f S16_LE -c %d -r %d -D plughw:%d 2>/dev/null",
		use_stereo ? 2 : 1, sample_freq_Hz, CardNumber);
	if (Verbose) std::cout << cmd << std::endl;
	aplay_pipe_ = popen(cmd, "w");
	if (aplay_pipe_ != nullptr)
	{
		// Bump the kernel pipe buffer so a full frame always fits and
		// fwrite() never blocks the producer loop. Two frames gives a
		// safety margin against scheduling jitter.
		int frame_bytes = (use_stereo ? 4 : 2) * sample_count;
		fcntl(fileno(aplay_pipe_), F_SETPIPE_SZ, frame_bytes * 2);
	}
	pipe_card_ = CardNumber;
}

void AudioData::closePipe()
{
	if (aplay_pipe_ != nullptr)
	{
		pclose(aplay_pipe_);
		aplay_pipe_ = nullptr;
		pipe_card_ = -1;
	}
}

void AudioData::wi(FILE* fp, uint16_t i)
{
	int b1, b0;
	b0 = i % 256;
	b1 = (i - b0) / 256;
	putc(b0, fp);
	putc(b1, fp);
}

void AudioData::wl(FILE* fp, uint32_t l)
{
	unsigned int i1, i0;
	i0 = l % 65536L;
	i1 = (l - i0) / 65536L;
	wi(fp, i0);
	wi(fp, i1);
}

void AudioData::SaveToWavFile(std::string filename)
{
	FILE *fp;
	int bytes_per_sample = (use_stereo ? 4 : 2);

	// Write 8/16-bit mono/stereo .wav file
	fp = fopen(filename.c_str(), "wb");
	fprintf(fp, "RIFF");
	wl(fp, sample_count * bytes_per_sample + 36L);
	fprintf(fp, "WAVEfmt ");
	wl(fp, 16L);
	wi(fp, 1);
	wi(fp, use_stereo ? 2 : 1);
	wl(fp, 0L + sample_freq_Hz);
	wl(fp, 0L + sample_freq_Hz * bytes_per_sample);
	wi(fp, bytes_per_sample);
	wi(fp, 16);
	fprintf(fp, "data");
	wl(fp, sample_count * bytes_per_sample);

	fwrite(samplebuffer.data(), bytes_per_sample, sample_count, fp);
	fclose(fp);
}

void AudioData::Play()
{
	updateVolume();

	pthread_mutex_lock(&audio_mutex);
	ensurePipeOpen();
	if (aplay_pipe_ != nullptr)
	{
		int bytes_per_sample = use_stereo ? 4 : 2;
		fwrite(samplebuffer.data(), bytes_per_sample, sample_count, aplay_pipe_);
		fflush(aplay_pipe_);
	}
	pthread_mutex_unlock(&audio_mutex);
}

int AudioData::PlayWav(std::string filename)
{
	char command[256] = "";
	int status;
	snprintf(command, 256, "aplay %s -D hw:%d", filename.c_str(), CardNumber);
	pthread_mutex_lock(&audio_mutex);
	closePipe(); // release device so external aplay can open it
	status = system(command);
	pthread_mutex_unlock(&audio_mutex);
	return status;
}

void AudioData::SetVolume(int newvolume)
{
	this->newvolume = newvolume;
}

int AudioData::updateVolume()
{
	if ((volume == newvolume) || (newvolume == -1))
	{
		return 0;
	}

	volume = newvolume;

	char command[256] = "";
	int status = 0;
	snprintf(command, 256, "amixer -c %d controls | grep MIXER | grep Playback | grep Volume | sed s/[^0-9]*//g", CardNumber);
	//std::cout << command << std::endl;

	pthread_mutex_lock(&audio_mutex);
	FILE *fp = popen(command, "r");
	if (fp == nullptr)
	{
		return -1;
	}
	char buffer[256];
	char *res;
	while (!feof(fp) && status == 0)
		{
		res = fgets(buffer, sizeof(buffer), fp);
		if ((res == nullptr) || atoi(res) == 0)
		{
			status = -1;
		}
		else
		{
			int numid = atoi(res);
			snprintf(command, sizeof(command), "amixer -c %d cset numid=%d %d%% -q", CardNumber, numid, newvolume);
			//std::cout << command << std::endl;

			status = system(command);
		}
	}
	pclose(fp);
	pthread_mutex_unlock(&audio_mutex);
	return status;
}

bool AudioData::Speak(std::string text)
{
	updateVolume();

	char command[1023] = "";
	int status;
	snprintf(command, 1023, "espeak --stdout \"%s\" | aplay -q -D plughw:%d", text.c_str(), CardNumber);
	pthread_mutex_lock(&audio_mutex);
	closePipe(); // release device so espeak|aplay can open it
	int res = system(command);
	pthread_mutex_unlock(&audio_mutex);
	return (res == 0);
}
