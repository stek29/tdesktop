/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

class FileLocation;

namespace Media {
namespace Clip {

enum class State {
	Reading,
	Error,
	Finished,
};

struct FrameRequest {
	bool valid() const {
		return factor > 0;
	}
	int factor = 0;
	int framew = 0;
	int frameh = 0;
	int outerw = 0;
	int outerh = 0;
	ImageRoundRadius radius = ImageRoundRadius::None;
};

enum ReaderSteps {
	WaitingForDimensionsStep = -3, // before ReaderPrivate read the first image and got the original frame size
	WaitingForRequestStep = -2, // before Reader got the original frame size and prepared the frame request
	WaitingForFirstFrameStep = -1, // before ReaderPrivate got the frame request and started waiting for the 1-2 delay
};

class ReaderPrivate;
class Reader {
public:
	using Callback = Function<void, Notification>;
	enum class Mode {
		Gif,
		Video,
	};

	Reader(const FileLocation &location, const QByteArray &data, Callback &&callback, Mode mode = Mode::Gif, int64 seekMs = 0);
	static void callback(Reader *reader, int threadIndex, Notification notification); // reader can be deleted

	void setAutoplay() {
		_autoplay = true;
	}
	bool autoplay() const {
		return _autoplay;
	}

	uint64 playId() const {
		return _playId;
	}
	int64 seekPositionMs() const {
		return _seekPositionMs;
	}

	void start(int framew, int frameh, int outerw, int outerh, ImageRoundRadius radius);
	QPixmap current(int framew, int frameh, int outerw, int outerh, uint64 ms);
	QPixmap frameOriginal() const {
		Frame *frame = frameToShow();
		if (!frame) return QPixmap();
		QPixmap result(frame ? QPixmap::fromImage(frame->original) : QPixmap());
		result.detach();
		return result;
	}
	bool currentDisplayed() const {
		Frame *frame = frameToShow();
		return frame ? (frame->displayed.loadAcquire() != 0) : true;
	}
	bool autoPausedGif() const {
		return _autoPausedGif.loadAcquire();
	}
	bool videoPaused() const;
	int threadIndex() const {
		return _threadIndex;
	}

	int width() const;
	int height() const;

	State state() const;
	bool started() const {
		int step = _step.loadAcquire();
		return (step == WaitingForFirstFrameStep) || (step >= 0);
	}
	bool ready() const;

	bool hasAudio() const;
	int64 getPositionMs() const;
	int64 getDurationMs() const;
	void pauseResumeVideo();

	void stop();
	void error();
	void finished();

	Mode mode() const {
		return _mode;
	}

	~Reader();

private:

	Callback _callback;
	Mode _mode;

	State _state = State::Reading;

	uint64 _playId;
	bool _hasAudio = false;
	int64 _durationMs = 0;
	int64 _seekPositionMs = 0;

	mutable int _width = 0;
	mutable int _height = 0;

	// -2, -1 - init, 0-5 - work, show ((state + 1) / 2) % 3 state, write ((state + 3) / 2) % 3
	mutable QAtomicInt _step = WaitingForDimensionsStep;
	struct Frame {
		void clear() {
			pix = QPixmap();
			original = QImage();
		}
		QPixmap pix;
		QImage original;
		FrameRequest request;
		QAtomicInt displayed = 0;

		// Should be counted from the end,
		// so that positionMs <= _durationMs.
		int64 positionMs = 0;
	};
	mutable Frame _frames[3];
	Frame *frameToShow(int *index = 0) const; // 0 means not ready
	Frame *frameToWrite(int *index = 0) const; // 0 means not ready
	Frame *frameToWriteNext(bool check, int *index = 0) const;
	void moveToNextShow() const;
	void moveToNextWrite() const;

	QAtomicInt _autoPausedGif = 0;
	QAtomicInt _videoPauseRequest = 0;
	int32 _threadIndex;

	bool _autoplay = false;

	friend class Manager;

	ReaderPrivate *_private = nullptr;

};

enum class ProcessResult {
	Error,
	Started,
	Finished,
	Paused,
	Repaint,
	CopyFrame,
	Wait,
};

class Manager : public QObject {
	Q_OBJECT

public:

	Manager(QThread *thread);
	int32 loadLevel() const {
		return _loadLevel.load();
	}
	void append(Reader *reader, const FileLocation &location, const QByteArray &data);
	void start(Reader *reader);
	void update(Reader *reader);
	void stop(Reader *reader);
	bool carries(Reader *reader) const;
	~Manager();

signals:
	void processDelayed();

	void callback(Media::Clip::Reader *reader, qint32 threadIndex, qint32 notification);

public slots:
	void process();
	void finish();

private:

	void clear();

	QAtomicInt _loadLevel;
	struct MutableAtomicInt {
		MutableAtomicInt(int value) : v(value) {
		}
		mutable QAtomicInt v;
	};
	typedef QMap<Reader*, MutableAtomicInt> ReaderPointers;
	ReaderPointers _readerPointers;
	mutable QReadWriteLock _readerPointersMutex;

	ReaderPointers::const_iterator constUnsafeFindReaderPointer(ReaderPrivate *reader) const;
	ReaderPointers::iterator unsafeFindReaderPointer(ReaderPrivate *reader);

	bool handleProcessResult(ReaderPrivate *reader, ProcessResult result, uint64 ms);

	enum ResultHandleState {
		ResultHandleRemove,
		ResultHandleStop,
		ResultHandleContinue,
	};
	ResultHandleState handleResult(ReaderPrivate *reader, ProcessResult result, uint64 ms);

	typedef QMap<ReaderPrivate*, uint64> Readers;
	Readers _readers;

	QTimer _timer;
	QThread *_processingInThread;
	bool _needReProcess;

};

MTPDocumentAttribute readAttributes(const QString &fname, const QByteArray &data, QImage &cover);

void Finish();

} // namespace Clip
} // namespace Media