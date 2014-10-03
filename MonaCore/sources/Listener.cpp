/*
Copyright 2014 Mona
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

This file is a part of Mona.
*/

#include "Mona/Listener.h"
#include "Mona/Publication.h"
#include "Mona/MediaCodec.h"
#include "Mona/StringReader.h"
#include "Mona/MIME.h"
#include "Mona/Logs.h"


using namespace std;

namespace Mona {

Listener::Listener(Publication& publication, Client& client, Writer& writer) : _writer(writer), publication(publication), receiveAudio(true), receiveVideo(true), client(client), _firstTime(true),
	_pAudioWriter(NULL),_pVideoWriter(NULL),_pDataWriter(NULL),_publicationNamePacket((const UInt8*)publication.name().c_str(),publication.name().size()),
	_startTime(0),_codecInfosSent(false) {
}

Listener::~Listener() {
	// -1 indicate that it come of the listener class
	if(_pAudioWriter)
		_pAudioWriter->close(-1);
	if(_pVideoWriter)
		_pVideoWriter->close(-1);
	if(_pDataWriter)
		_pDataWriter->close(-1);
}

const QualityOfService& Listener::audioQOS() const {
	if(!_pAudioWriter)
		return QualityOfService::Null;
	return _pAudioWriter->qos();
}

const QualityOfService& Listener::videoQOS() const {
	if(!_pVideoWriter)
		return QualityOfService::Null;
	return _pVideoWriter->qos();
}

const QualityOfService& Listener::dataQOS() const {
	if(!_pDataWriter)
		return QualityOfService::Null;
	return _pDataWriter->qos();
}

bool Listener::init() {
	if (_pVideoWriter || _pAudioWriter || _pDataWriter) {
		WARN("Reinitialisation of one ", publication.name(), " subscription");
		_pVideoWriter = _pAudioWriter = _pDataWriter = NULL;
	}
	if (!_writer.writeMedia(Writer::INIT, 0, publicationNamePacket(),*this))// unsubscribe can be done here!
		return false; // Here consider that the listener have to be closed by the caller

	_pAudioWriter = &_writer.newWriter();
	_pAudioWriter->writeMedia(Writer::INIT, Writer::AUDIO, publicationNamePacket(),*this);
	_pVideoWriter = &_writer.newWriter();
	_pVideoWriter->writeMedia(Writer::INIT, Writer::VIDEO, publicationNamePacket(),*this);
	_pDataWriter = &_writer.newWriter();
	_pDataWriter->writeMedia(Writer::INIT, Writer::DATA, publicationNamePacket(),*this);

	
	if(!publication.audioCodecBuffer().empty()) {
		PacketReader audioCodecPacket(publication.audioCodecBuffer()->data(),publication.audioCodecBuffer()->size());
		INFO("AAC codec infos sent to one listener of ", publication.name(), " publication")
		pushAudio(_startTime,audioCodecPacket);
	}

	if (getBool<false>("unbuffered")) {
		_pAudioWriter->reliable = false;
		_pVideoWriter->reliable = false;
		_pDataWriter->reliable = false;
	}

	string converstionType;
	if (getString("conversionType", converstionType) || !MIME::CreateDataWriter(converstionType.c_str(), publication.poolBuffers, _pConvertorWriter))
		_pConvertorWriter.reset();

	return true;
}

void Listener::pushData(DataReader& reader) {
	if (!_pDataWriter && !init())
		return;

	/* TODO remplacer par un relay mode � imaginer et concevoir!
	if(publication.publisher()) {
		if(ICE::ProcessSDPPacket(reader,(Peer&)*publication.publisher(),publication.publisher()->writer(),(Peer&)client,*_pDataWriter))
			return;
	}*/

	// If un conversion type exists, and it is different than the current reader gotten
	if (_pConvertorWriter && typeid(*_pConvertorWriter).name()!=typeid(reader).name()) {
		// convert data
		reader.read(*_pConvertorWriter);
		PacketReader packet(_pConvertorWriter->packet.data(),_pConvertorWriter->packet.size());
		if(!_pDataWriter->writeMedia(Writer::DATA,0,packet,*this))
			init();
		_pConvertorWriter->clear();
		return;
	}

	if (!reader) {
		ERROR("Impossible to push ", typeid(reader).name(), " null DataReader without an explicit conversion");
		return;
	}

	if(!_pDataWriter->writeMedia(Writer::DATA,0,reader.packet,*this))
		init();
}


void Listener::pushVideo(UInt32 time,PacketReader& packet) {
	if(!receiveVideo && !MediaCodec::H264::IsCodecInfos(packet))
		return;

	if (!_codecInfosSent) {
		if (MediaCodec::IsKeyFrame(packet)) {
			if (!publication.videoCodecBuffer().empty() && !MediaCodec::H264::IsCodecInfos(packet)) {
				PacketReader videoCodecPacket(publication.videoCodecBuffer()->data(), publication.videoCodecBuffer()->size());
				INFO("H264 codec infos sent to one listener of ", publication.name(), " publication")
				pushVideo(time, videoCodecPacket);
			}
			_codecInfosSent = true;
		} else if (_firstTime) {
			DEBUG("Video frame dropped to wait first key frame");
			return;
		}
	}
	

	if (!_pVideoWriter && !init())
		return;

	if (_firstTime) {
		_startTime = time;
		_firstTime = false;
	}
	time -= _startTime;

	TRACE("Video time ", time);

	if(!_pVideoWriter->writeMedia(Writer::VIDEO,time,packet,*this))
		init();
}


void Listener::pushAudio(UInt32 time,PacketReader& packet) {
	if(!receiveAudio && !MediaCodec::AAC::IsCodecInfos(packet))
		return;

	if (!_pAudioWriter && !init())
		return;

	if (_firstTime) {
		_startTime = time;
		_firstTime = false;
	}
	time -= _startTime;

	TRACE("Audio time ", time);

	if(!_pAudioWriter->writeMedia(Writer::AUDIO,time,packet,*this))
		init();
}

void Listener::pushProperties(const Parameters& properties,PacketReader& infos) {
	if (!init())
		return;
	
	if (!_writer.writeMedia(Writer::PROPERTIES, properties.count(), infos,properties))
		init();
}

void Listener::flush() {
	if(_pAudioWriter)
		_pAudioWriter->flush();
	if(_pVideoWriter)
		_pVideoWriter->flush();
	if(_pDataWriter)
		_pDataWriter->flush();
	_writer.flush(true);
}


} // namespace Mona
