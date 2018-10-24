//
//  EngineAndroid.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 26/2/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include "../Engine.h"

#include <sys/stat.h>


namespace HQRemote {
	struct Engine::Impl {
	};
	
	void Engine::platformConstruct() {
		m_impl = new Impl();
	}
	void Engine::platformDestruct() {
		delete m_impl;
	}
	
	std::string Engine::platformGetWritableFolder() {
		const char directory[] = "/sdcard/.hqremotecontroller";
		
		mkdir(directory, 0755);
		
		return directory;
	}
	
	std::string Engine::platformGetAppName() {
		//TODO
		return "noname";
	}
	
	void Engine::platformStartRecording() {
		//TODO
	}
	
	void Engine::platformRecordFrame(double t, const CapturedFrame& frame) {
		//TODO
	}
	
	void Engine::platformEndRecording() {
		//TODO
	}
}
