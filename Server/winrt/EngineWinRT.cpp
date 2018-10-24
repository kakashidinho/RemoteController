#include "../Engine.h"

#include <codecvt>


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
		Windows::ApplicationModel::Package^ package = Windows::ApplicationModel::Package::Current;
		Windows::ApplicationModel::PackageId^ packageId = package->Id;

		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		auto name = converter.to_bytes(packageId->Name->Data());

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