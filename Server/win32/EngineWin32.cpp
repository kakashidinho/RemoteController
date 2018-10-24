////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016-2018 Le Hoang Quyen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//		http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////

#include "../Engine.h"



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
		CreateDirectory(L"../HQRemoteControllerData/", NULL);
		return "../HQRemoteControllerData/";
	}

	std::string Engine::platformGetAppName() {
		char buf[MAX_PATH];
		GetModuleFileNameA(NULL, buf, MAX_PATH);

		std::string name = buf;
		auto slashPos = name.find_last_of('\\');
		if (slashPos == std::string::npos)
			slashPos = name.find_last_of('/');
		if (slashPos != std::string::npos)
			name = name.substr(slashPos + 1);

		return name;
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