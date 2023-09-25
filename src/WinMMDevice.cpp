/**
 * @file 	WinMMDevice.cpp
 *
 * @date 	19.1.2013
 * @author 	Zdenek Travnicek <travnicek@iim.cz>
 * @copyright GNU Public License 3.0
 *
 */

#include "iimavlib/WinMMDevice.h"
#include "iimavlib/WinMMError.h"
#include "iimavlib/Utils.h"
#include <map>
#include <stdexcept>
#include <algorithm>

#pragma comment(lib, "winmm.lib")
namespace iimavlib {

namespace {
void CALLBACK win_mm_device_capture_callback (HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	//logger[log_level::info] << "CALLBACK, msg: " << uMsg << "\n";
	if (uMsg == WIM_DATA) {
		WinMMDevice *winmm = reinterpret_cast<WinMMDevice*>(dwInstance);
		WAVEHDR *hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
		winmm->store_data(*hdr);
	}
}
void CALLBACK win_mm_device_playback_callback (HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	//logger[log_level::info] << "Playback CALLBACK, msg: " << uMsg;
	if (uMsg == WOM_DONE) {
		
		WinMMDevice *winmm = reinterpret_cast<WinMMDevice*>(dwInstance);
		WAVEHDR *hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
		logger[log_level::info] << "Returning buffer 0x" << std::hex << hdr;
		winmm->store_data(*hdr);
	}
}

}


WinMMDevice::WinMMDevice(action_type_t action, audio_id_t id, const audio_params_t& params):
	GenericDevice(),action_(action),id_(id),params_(params),private_buffer_(1048576)
{
	sampling_rate_ 		= convert_rate_to_int(params_.rate);
	bps_ 				= 16;//paramsget_sample_size(params_.format) * 8;

	WAVEFORMATEX fmt;
	fmt.wFormatTag		= WAVE_FORMAT_PCM;
	fmt.nChannels		= number_of_channels;
	fmt.nSamplesPerSec	= sampling_rate_;
	fmt.nAvgBytesPerSec	= sampling_rate_*number_of_channels*bps_/8;   // = nSamplesPerSec * n.Channels * wBitsPerSample/8
	fmt.nBlockAlign		= number_of_channels*bps_/8;                  // = n.Channels * wBitsPerSample/8
	fmt.wBitsPerSample	= bps_;
	fmt.cbSize			= 0;

	switch (action) {
		case action_type_t::action_capture:
			init_capture(fmt);
			break;
		case action_type_t::action_playback:
			init_playback(fmt);
			break;
		default:
			throw_call(false, "Unsupported action!");
	}

}

void WinMMDevice::init_capture(WAVEFORMATEX& fmt)
{
	buffers.resize(capture_buffer_count);
	buffer_length = capture_buffer_length;
	throw_call(waveInOpen(&in_handle,			// Input handle
					id_ ,						// Device ID
					&fmt,						// Input format
					reinterpret_cast<DWORD_PTR>(&win_mm_device_capture_callback), 
												// Callback
					reinterpret_cast<DWORD_PTR>(this),
												// Parameter to callback
					WAVE_FORMAT_DIRECT|CALLBACK_FUNCTION),	// Mode
			"Failed to open input device");
	//private_buffer_.data.resize(buffer_count*buffer_length*2);

	for(auto& hdr: buffers) init_in_buffer(hdr);
	logger[log_level::debug] << "Opened device and added " << buffers.size() << " buffers";
}

void WinMMDevice::init_playback(WAVEFORMATEX& fmt)
{
	logger[log_level::debug] << "Initializing playback.  " << fmt.nChannels << " chan, " 
		<< fmt.wBitsPerSample << " bps, " << fmt.nAvgBytesPerSec << " BPS, " <<
		fmt.nSamplesPerSec << "SPS";
	WAVEOUTCAPS caps;
	throw_call (waveOutGetDevCaps(id_,&caps,sizeof(WAVEOUTCAPS)),"Failed to query output device");
	logger[log_level::info] << "Using output device: " << caps.szPname 
		<< ", driver version: " << caps.vDriverVersion << "\n"
		<< "Device has " << caps.wChannels << " channels";
	
	throw_call(waveOutOpen(&out_handle,			// Input handle
					id_ ,						// Device ID
					&fmt,						// Input format
					reinterpret_cast<DWORD_PTR>(&win_mm_device_playback_callback), 
												// Callback
					reinterpret_cast<DWORD_PTR>(this),
												// Parameter to callback
					WAVE_FORMAT_DIRECT|CALLBACK_FUNCTION),	// Mode
			"Failed to open input device");
	waveOutReset(out_handle);
	waveOutSetPlaybackRate(out_handle,0x00010000);
	waveOutPause(out_handle);
	logger[log_level::debug] << "Opened and configured output device.";
}
void WinMMDevice::init_in_buffer(WAVEHDR& hdr)
{
	hdr.lpData = (LPSTR)new uint8_t[buffer_length];
	hdr.dwBufferLength = static_cast<DWORD>(buffer_length);
	hdr.dwBytesRecorded=0;
	hdr.dwUser = 0L;
	hdr.dwFlags = 0L;
	hdr.dwLoops = 0L;
	throw_call(waveInPrepareHeader(in_handle, &hdr, sizeof(WAVEHDR)),"Failed to prepare buffer");
	throw_call(waveInAddBuffer(in_handle, &hdr, sizeof(WAVEHDR)),"Failed to add buffer");
}
void WinMMDevice::init_out_buffer(WAVEHDR& hdr)
{
	hdr.lpData = (LPSTR)nullptr;//new uint8_t[buffer_length];
	hdr.dwBufferLength = 0;//static_cast<DWORD>(buffer_length);
	hdr.dwBytesRecorded=0;
	hdr.dwUser = 0L;
	hdr.dwFlags = 0L;
	hdr.dwLoops = 0L;
	throw_call(waveOutPrepareHeader(out_handle, &hdr, sizeof(WAVEHDR)),"Failed to prepare buffer");
	logger[log_level::debug] << "Out buffer prepared";
	//throw_call(waveOutAddBuffer(out_handle, &hdr, sizeof(WAVEHDR)),"Failed to add buffer");
}
WinMMDevice::~WinMMDevice()
{
}

WinMMDevice::audio_id_t WinMMDevice::default_device()
{
	return WAVE_MAPPER;
}

error_type_t WinMMDevice::do_start_capture() {
	if(!check_call(waveInStart(in_handle),"Failed to start capture")) return error_type_t::failed;
	logger[log_level::debug] << "Capture started";
	return error_type_t::ok;
}

size_t WinMMDevice::do_capture_data(audio_sample_t* data_start, size_t data_size, error_type_t& error_code) 
{
	std::vector<WAVEHDR*> tmp_hdr;
	{
		std::lock_guard<std::mutex> l(buffer_lock_);
		while (!empty_buffers.empty()) {
			WAVEHDR& hdr = *empty_buffers.back();
			check_call(waveInUnprepareHeader(in_handle, &hdr, sizeof(WAVEHDR)),"Failed to unprepare buffer");
			private_buffer_.store_data(reinterpret_cast<audio_sample_t*>(hdr.lpData),hdr.dwBytesRecorded/sizeof(audio_sample_t));
			//logger[log_level::debug] << "Stored "<< hdr.dwBytesRecorded << " bytes into circular buffer";
			tmp_hdr.push_back(&hdr);
			empty_buffers.pop_back();
		}
	}
	for (WAVEHDR*hdr:tmp_hdr) {
		check_call(waveInPrepareHeader(in_handle, hdr, sizeof(WAVEHDR)),"Failed to prepare buffer");
		check_call(waveInAddBuffer(in_handle, hdr, sizeof(WAVEHDR)),"Failed to add buffer");
	}
	std::size_t ret = private_buffer_.get_data_block(data_start,data_size);
	if (ret == 0) error_code = error_type_t::buffer_empty;
	else error_code = error_type_t::ok;
	return ret/*/params_.sample_size()*/;
}
error_type_t WinMMDevice::do_set_buffers(uint16_t count, uint32_t samples) 
{
	std::lock_guard<std::mutex> l(buffer_lock_);
	buffers.resize(count);
	buffer_length = samples * params_.sample_size();
	for(auto& hdr: buffers) init_out_buffer(hdr);
	for(auto& hdr: buffers) empty_buffers.push_back(&hdr);
	logger[log_level::debug] << "Added " << buffers.size() << " buffers for " << samples << " samples each";
	return error_type_t::ok;
}
void WinMMDevice::store_data(WAVEHDR& hdr)
{
	std::lock_guard<std::mutex> l(buffer_lock_);
	empty_buffers.push_back(&hdr);
	//logger[log_level::debug] << "Empty buffer count: " << empty_buffers.size();
}
error_type_t WinMMDevice::do_fill_buffer(const audio_sample_t* data_start, size_t data_size) 
{
	size_t size = data_size * sizeof(audio_sample_t);
	WAVEHDR* hdr = nullptr;
	{
		std::lock_guard<std::mutex> l(buffer_lock_);
		if (empty_buffers.empty()) return error_type_t::buffer_full;
		hdr = empty_buffers.back();
		empty_buffers.pop_back();
	}
	logger[log_level::debug] << "Writing samples to the device";
	
	check_call(waveOutUnprepareHeader(out_handle, hdr, sizeof(WAVEHDR)),"Failed to unprepare buffer");
	if (hdr->dwBufferLength != size ) {
		delete [] hdr->lpData;
		hdr->lpData = (LPSTR)(new uint8_t[size]);
		hdr->dwBufferLength = static_cast<DWORD>(buffer_length);
		hdr->dwBytesRecorded= static_cast<DWORD>(buffer_length);
	}
	hdr->dwUser = 0L;
	hdr->dwFlags = 0L;
	hdr->dwLoops = 0L;
	check_call(waveOutPrepareHeader(out_handle, hdr, sizeof(WAVEHDR)),"Failed to prepare buffer");
	std::copy_n(reinterpret_cast<const uint8_t*>(data_start), size, hdr->lpData);
	logger[log_level::debug] << "Copied " << size << " bytes";
	logger[log_level::info] << "Sending buffer 0x" << std::hex << hdr;
	if (MMSYSERR_NOERROR  == waveOutWrite(out_handle,hdr,sizeof(WAVEHDR))) 	{
		//logger[log_level::debug] << "OK";
		DWORD rate=0;

		if (check_call(waveOutGetPlaybackRate(out_handle,&rate),"Querying playback rate")) {
			double frate = (rate>>16)+(static_cast<double>(rate&0xFFFF)/0xFFFF);
			logger[log_level::debug] << "Playback rate: " << frate << ", raw: 0x"<<std::hex <<rate;
		}
		
		return error_type_t::ok;
	}
	logger[log_level::debug] << "Failed to write samples to the device!!";
	return error_type_t::failed;
}

error_type_t WinMMDevice::do_update(size_t delay) 
{
	return error_type_t::ok;
}
error_type_t WinMMDevice::do_start_playback()
{
	logger[log_level::debug] << "Starting the device";
	if (waveOutRestart(out_handle)==MMSYSERR_NOERROR) {
		logger[log_level::debug] << "Playback started";
		return error_type_t::ok;
	}
	logger[log_level::debug] << "Failed to start the device";
	return error_type_t::failed;
}
namespace {
audio_info_t get_in_info(UINT dev)
{
	audio_info_t info_;
	WAVEINCAPS caps_;
	waveInGetDevCaps (dev,&caps_,sizeof(WAVEINCAPS));
	//info_.max_channels = caps_.wChannels;
	info_.name = caps_.szPname;
	info_.default_ = false;
	return info_;
}
audio_info_t get_out_info(UINT dev)
{
	audio_info_t info_;
	WAVEOUTCAPS caps_;
	waveOutGetDevCaps (dev,&caps_,sizeof(WAVEOUTCAPS));
	//info_.max_channels = caps_.wChannels;
	info_.name = caps_.szPname;
	info_.default_ = false;
	return info_;
}
}
std::map<WinMMDevice::audio_id_t, audio_info_t> WinMMDevice::do_enumerate_capture_devices() 
{
	std::map<audio_id_t, audio_info_t> devices;
	devices[WAVE_MAPPER]=get_in_info(WAVE_MAPPER);
	UINT num_dev = waveInGetNumDevs();
	for (UINT i=0;i<num_dev;++i) devices[i]=get_in_info(i);
	
	devices[default_device()].default_=true;
	return devices;
}
std::map<WinMMDevice::audio_id_t, audio_info_t> WinMMDevice::do_enumerate_playback_devices()
{
	std::map<audio_id_t, audio_info_t> devices;
	devices[WAVE_MAPPER]=get_out_info(WAVE_MAPPER);
	UINT num_dev = waveOutGetNumDevs();
	for (UINT i=0;i<num_dev;++i) devices[i]=get_out_info(i);

	devices[default_device()].default_=true;
	return devices;
}

}
