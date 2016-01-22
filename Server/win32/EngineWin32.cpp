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

	void Engine::platformRecordFrame(double t, ConstDataRef frame) {
		//TODO
	}

	void Engine::platformEndRecording() {
		//TODO
	}
}