/**
 * @file 	SDLDevice.cpp
 *
 * @date 	23.2.2013
 * @author 	Zdenek Travnicek <travnicek@iim.cz>
 * @copyright GNU Public License 3.0
 *
 */


#include "SDL.h"
#include "iimavlib/SDLDevice.h"
#include "iimavlib/Utils.h"
#ifdef SYSTEM_LINUX
#include <unistd.h>
#endif
namespace iimavlib {

struct SDLDeleter {
	void operator()(SDL_Surface*obj){SDL_FreeSurface(obj);}
};
struct sdl_pimpl_t {
	std::unique_ptr<SDL_Surface,SDLDeleter> window_;
};

SDLDevice::SDLDevice(size_t width, size_t height, const std::string& title):
		width_(width),height_(height),title_(title),finish_(false),
		data_changed_(false),flip_required_(false)
{
	static_assert(sizeof(RGB)==3,"Wrongly packed RGB struct!");
	
	data_.resize(width*height);
	pimpl_.reset(new sdl_pimpl_t());
}

SDLDevice::~SDLDevice()
{
	stop();
	SDL_Quit();
}

bool SDLDevice::start()
{
	std::unique_lock<std::mutex> lock(thread_mutex_);
	if (thread_.joinable()) return true;
	thread_ = std::thread([=](){this->run();});
	logger[log_level::debug] << "SDL thread started";
	if (thread_.joinable()) return true;
	return false;
}

bool SDLDevice::stop()
{
	std::unique_lock<std::mutex> lock(thread_mutex_);
	if (!thread_.joinable()) return true;
	finish_ = true;
	thread_.join();
	
	logger[log_level::debug] << "SDL thread joined";
	return true;

}

void SDLDevice::run()
{
	SDL_Init(SDL_INIT_VIDEO);
	logger[log_level::debug] << "Creating SDL window";
	pimpl_->window_.reset(SDL_SetVideoMode(static_cast<int>(width_), static_cast<int>(height_),
			24, SDL_DOUBLEBUF));
	if (!pimpl_->window_) {
		logger[log_level::fatal] << "Failed to create SDL window!";
		finish_=true;
	} else {
		logger[log_level::debug] << "SDL window created";
	}
	SDL_WM_SetCaption(title_.c_str(),title_.c_str());
	
	while (process_events()) {
		update_data();
		if (flip_required_) {
			SDL_Flip(pimpl_->window_.get());
			flip_required_ = false;
		} else {
#ifdef SYSTEM_LINUX
			usleep(5000);
#endif
		}
	}
	pimpl_->window_.reset();
}
void SDLDevice::update_data()
{
	std::unique_lock<std::mutex> lock(data_mutex_);
	if (data_changed_) {
		SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(&data_[0],static_cast<int>(width_),static_cast<int>(height_),24,0,0x0000FF,0x00FF00,0xFF0000,0);
		SDL_BlitSurface(surface, nullptr, pimpl_->window_.get(), nullptr);
		SDL_FreeSurface(surface);
		flip_required_ = true;
	}
}
bool SDLDevice::process_events()
{
	if(!finish_) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch(event.type) {
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_ESCAPE) {
						finish_ = true;
						logger[log_level::debug] << "ESC pressed.";
					} break;
				case SDL_QUIT:
					finish_ = true;
					logger[log_level::debug] << "Quit event received.";
					break;
				case SDL_VIDEOEXPOSE: {
					logger[log_level::debug] << "Video expose";
					std::unique_lock<std::mutex> lock(data_mutex_);
					data_changed_ = true;
				} break;
			}
		}
	}
	return !finish_;
}
template<>
bool SDLDevice::update(const data_type& data) {
	if (finish_) return false;
	std::unique_lock<std::mutex> lock(data_mutex_);
	if (data.size()>data_.size()) data_.resize(data.size());
	std::copy(data.begin(),data.end(),data_.begin());
	data_changed_ = true;
	return true;
}
}