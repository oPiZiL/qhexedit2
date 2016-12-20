#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>

#include "qhexedit.h"

// ********************************************************************** Constructor, destructor

QHexEdit::QHexEdit(QWidget *parent) : QAbstractScrollArea(parent)
{
    _addressArea = true;
    _addressWidth = 4;
    _asciiArea = true;
    _overwriteMode = true;
    _highlighting = true;
    _readOnly = false;
    _cursorPosition = 0;
    _lastEventSize = 0;

    _chunks = new Chunks(this);
    _undoStack = new UndoStack(_chunks, this);
#ifdef Q_OS_WIN32
    setFont(QFont("Courier", 10));
#else
    setFont(QFont("Monospace", 10));
#endif
    setAddressAreaColor(this->palette().alternateBase().color());
    setHighlightingColor(QColor(0xff, 0xff, 0x99, 0xff));
    setSelectionColor(this->palette().highlight().color());

    connect(&_cursorTimer, SIGNAL(timeout()), this, SLOT(updateCursor()));
    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(_undoStack, SIGNAL(indexChanged(int)), this, SLOT(dataChangedPrivate(int)));

    _cursorTimer.setInterval(500);
    _cursorTimer.start();

    setBytesPerLine(16);
    setAddressWidth(4);
    setAddressArea(true);
    setAsciiArea(true);
    setOverwriteMode(true);
    setHighlighting(true);
    setReadOnly(false);

    init();
}

QHexEdit::~QHexEdit()
{
}

// ********************************************************************** Properties

void QHexEdit::setAddressArea(bool addressArea)
{
    _addressArea = addressArea;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

bool QHexEdit::addressArea()
{
    return _addressArea;
}

void QHexEdit::setAddressAreaColor(const QColor &color)
{
    _addressAreaColor = color;
    viewport()->update();
}

QColor QHexEdit::addressAreaColor()
{
    return _addressAreaColor;
}

void QHexEdit::setAddressOffset(qint64 addressOffset)
{
    _addressOffset = addressOffset;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

qint64 QHexEdit::addressOffset()
{
    return _addressOffset;
}

void QHexEdit::setAddressWidth(int addressWidth)
{
    _addressWidth = addressWidth;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

int QHexEdit::addressWidth()
{
    qint64 size = _chunks->size();
    int n = 1;
    if (size > Q_INT64_C(0x100000000)){ n += 8; size /= Q_INT64_C(0x100000000);}
    if (size > 0x10000){ n += 4; size /= 0x10000;}
    if (size > 0x100){ n += 2; size /= 0x100;}
    if (size > 0x10){ n += 1; size /= 0x10;}

    if (n > _addressWidth)
        return n;
    else
        return _addressWidth;
}

void QHexEdit::setAsciiArea(bool asciiArea)
{
    _asciiArea = asciiArea;
    viewport()->update();
}

bool QHexEdit::asciiArea()
{
    return _asciiArea;
}

void QHexEdit::setCursorPosition(const QHexEdit::CursorPosition & position)
{
	CursorPosition cPos = position;
    // 1. delete old cursor
    _blink = false;
    viewport()->update(_cursorRect);

	if (cPos < 0)
		cPos = 0;

	// 2. Check, if cursor in range?
	if (cPos.isAscii() == false){
		if (cPos > (_chunks->size() * 2 - 1))
			cPos = _chunks->size() * 2 - (_overwriteMode ? 1 : 0);
	} else{
		// 2. Check, if cursor in range?
		if (cPos > (_chunks->size() - 1))
			cPos = _chunks->size() - (_overwriteMode ? 1 : 0);
	}

	// 3. Calc new position of cursor
    _cursorPosition = cPos;
	
	if (cPos.isAscii() == false){ // hex
		_bPosCurrent = cPos / 2;
		_pxCursorY = ((cPos / 2 - _bPosFirst) / _bytesPerLine + 1) * _pxCharHeight;
		int x = (cPos % (2 * _bytesPerLine));
		_pxCursorX = (((x / 2) * 3) + (x % 2)) * _pxCharWidth + _pxPosHexX;
	} else { // ascii
		_bPosCurrent = cPos;
		_pxCursorY = ((cPos - _bPosFirst) / _bytesPerLine + 1) * _pxCharHeight;
		int x = (cPos % _bytesPerLine ) ;
		_pxCursorX = x * _pxCharWidth + _pxPosAsciiX;
	}

    if (_overwriteMode)
        _cursorRect = QRect(_pxCursorX - horizontalScrollBar()->value(), _pxCursorY + _pxCursorWidth, _pxCharWidth, _pxCursorWidth);
    else
        _cursorRect = QRect(_pxCursorX - horizontalScrollBar()->value(), _pxCursorY - _pxCharHeight + 4, _pxCursorWidth, _pxCharHeight);

    // 4. Immediately draw new cursor
    _blink = true;
    viewport()->update(_cursorRect);
    emit currentAddressChanged(_bPosCurrent);
}

QHexEdit::CursorPosition QHexEdit::cursorPosition(QPoint pos)
{
    // Calc cursor position depending on a graphical position
	CursorPosition result(-1);
    int posX = pos.x() + horizontalScrollBar()->value();
    int posY = pos.y() - 3;
    if ((posX >= _pxPosHexX) && (posX < (_pxPosHexX + (1 + _hexCharsInLine) * _pxCharWidth)))
    {
		int x = (posX - _pxPosHexX ) / _pxCharWidth;
        x = (x / 3) * 2 + x % 3;
        int y = (posY / _pxCharHeight) * 2 * _bytesPerLine;
        result = _bPosFirst * 2 + x + y;
    } else
		if ((posX >= _pxPosAsciiX) && (posX < (_pxPosAsciiX + (1 + _bytesPerLine) * _pxCharWidth)))
		{
			int x = (posX - _pxPosAsciiX ) / _pxCharWidth;
//			x = (x / 3) * 2 + x % 3;
			int y = (posY / _pxCharHeight) * _bytesPerLine;
			result = _bPosFirst * 2 + x + y;
			result.setIsAscii(true);
			qDebug() << "x=" << x << " y=" << y << " result=" << result.pos();
		}
    return result;
}

QHexEdit::CursorPosition QHexEdit::cursorPosition()
{
    return _cursorPosition;
}

void QHexEdit::setData(const QByteArray &ba)
{
    _data = ba;
    _bData.setData(_data);
    setData(_bData);
}

QByteArray QHexEdit::data()
{
    return _chunks->data(0, -1);
}

void QHexEdit::setHighlighting(bool highlighting)
{
    _highlighting = highlighting;
    viewport()->update();
}

bool QHexEdit::highlighting()
{
    return _highlighting;
}

void QHexEdit::setHighlightingColor(const QColor &color)
{
    _brushHighlighted = QBrush(color);
    _penHighlighted = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}

QColor QHexEdit::highlightingColor()
{
    return _brushHighlighted.color();
}

void QHexEdit::setOverwriteMode(bool overwriteMode)
{
    _overwriteMode = overwriteMode;
    emit overwriteModeChanged(overwriteMode);
}

bool QHexEdit::overwriteMode()
{
    return _overwriteMode;
}

void QHexEdit::setSelectionColor(const QColor &color)
{
    _brushSelection = QBrush(color);
    _penSelection = QPen(Qt::white);
    viewport()->update();
}

int QHexEdit::bytesPerLine()
{
  return _bytesPerLine;
}

void QHexEdit::setBytesPerLine(int count)
{
  _bytesPerLine = count;
  _hexCharsInLine = count * 3 - 1;

  adjust();
  setCursorPosition(_cursorPosition);
  viewport()->update();
}

QColor QHexEdit::selectionColor()
{
    return _brushSelection.color();
}

bool QHexEdit::isReadOnly()
{
    return _readOnly;
}

void QHexEdit::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
}

// ********************************************************************** Access to data of qhexedit
bool QHexEdit::setData(QIODevice &iODevice)
{
    bool ok = _chunks->setIODevice(iODevice);
    init();
    dataChangedPrivate();
    return ok;
}

QByteArray QHexEdit::dataAt(qint64 pos, qint64 count)
{
    return _chunks->data(pos, count);
}

bool QHexEdit::write(QIODevice &iODevice, qint64 pos, qint64 count)
{
    return _chunks->write(iODevice, pos, count);
}

// ********************************************************************** Char handling
void QHexEdit::insert(qint64 index, char ch)
{
    _undoStack->insert(index, ch);
    refresh();
}

void QHexEdit::remove(qint64 index, qint64 len)
{
    _undoStack->removeAt(index, len);
    refresh();
}

void QHexEdit::replace(qint64 index, char ch)
{
    _undoStack->overwrite(index, ch);
    refresh();
}

// ********************************************************************** ByteArray handling
void QHexEdit::insert(qint64 pos, const QByteArray &ba)
{
    _undoStack->insert(pos, ba);
    refresh();
}

void QHexEdit::replace(qint64 pos, qint64 len, const QByteArray &ba)
{
    _undoStack->overwrite(pos, len, ba);
    refresh();
}

// ********************************************************************** Utility functions
void QHexEdit::ensureVisible()
{
    if (_cursorPosition < (_bPosFirst * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine));
    if (_cursorPosition > ((_bPosFirst + (_rowsShown - 1)*_bytesPerLine) * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine) - _rowsShown + 1);
    if (_pxCursorX < horizontalScrollBar()->value())
        horizontalScrollBar()->setValue(_pxCursorX);
    if ((_pxCursorX + _pxCharWidth) > (horizontalScrollBar()->value() + viewport()->width()))
        horizontalScrollBar()->setValue(_pxCursorX + _pxCharWidth - viewport()->width());
    viewport()->update();
}

qint64 QHexEdit::indexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->indexOf(ba, from);
    if (pos > -1)
    {
        qint64 curPos = pos*2;
        setCursorPosition(curPos + ba.length()*2);
        resetSelection(curPos);
        setSelection(curPos + ba.length()*2);
        ensureVisible();
    }
    return pos;
}

bool QHexEdit::isModified()
{
    return _modified;
}

qint64 QHexEdit::lastIndexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->lastIndexOf(ba, from);
    if (pos > -1)
    {
        qint64 curPos = pos*2;
        setCursorPosition(curPos - 1);
        resetSelection(curPos);
        setSelection(curPos + ba.length()*2);
        ensureVisible();
    }
    return pos;
}

void QHexEdit::redo()
{
    _undoStack->redo();
    setCursorPosition(_chunks->pos()*2);
    refresh();
}

QString QHexEdit::selectionToReadableString()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
    return toReadable(ba);
}

void QHexEdit::setFont(const QFont &font)
{
    QWidget::setFont(font);
    _pxCharWidth = fontMetrics().width(QLatin1Char('2'));
    _pxCharHeight = fontMetrics().height();
    _pxGapAdr = _pxCharWidth / 2;
    _pxGapAdrHex = _pxCharWidth;
    _pxGapHexAscii = 2 * _pxCharWidth;
    _pxCursorWidth = _pxCharHeight / 7;
    _pxSelectionSub = _pxCharHeight / 5;
    viewport()->update();
}

QString QHexEdit::toReadableString()
{
    QByteArray ba = _chunks->data();
    return toReadable(ba);
}

void QHexEdit::undo()
{
    _undoStack->undo();
    setCursorPosition(_chunks->pos()*2);
    refresh();
}

// ********************************************************************** Handle events
void QHexEdit::keyPressEventAscii(QKeyEvent *event)
{
	// Cursor movements
	if (event->matches(QKeySequence::MoveToNextChar))
	{
		setCursorPosition(CursorPosition(_cursorPosition + 1,true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousChar))
	{
		setCursorPosition(CursorPosition(_cursorPosition - 1,true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToEndOfLine))
	{
		setCursorPosition(CursorPosition(_cursorPosition | (_bytesPerLine - 1),false));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToStartOfLine))
	{
		setCursorPosition(CursorPosition(_cursorPosition - (_cursorPosition % ( _bytesPerLine))));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousLine))
	{
		setCursorPosition(CursorPosition(_cursorPosition - ( _bytesPerLine),true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToNextLine))
	{
		setCursorPosition(CursorPosition(_cursorPosition + ( _bytesPerLine),true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToNextPage))
	{
		setCursorPosition(CursorPosition(_cursorPosition + (((_rowsShown - 1)  * _bytesPerLine)),true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousPage))
	{
		setCursorPosition(CursorPosition(_cursorPosition - (((_rowsShown - 1)  * _bytesPerLine)),true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToEndOfDocument))
	{
		setCursorPosition(CursorPosition(_chunks->size(),true));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToStartOfDocument))
	{
		setCursorPosition(0);
		resetSelection(_cursorPosition);
	}

	// Select commands
	if (event->matches(QKeySequence::SelectAll))
	{
		resetSelection(0);
		setSelection( CursorPosition( _chunks->size() + 1,true));
	}
	if (event->matches(QKeySequence::SelectNextChar))
	{
		CursorPosition pos = CursorPosition(_cursorPosition + 1,true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousChar))
	{
		CursorPosition pos = CursorPosition(_cursorPosition - 1,true);
		setSelection(pos);
		setCursorPosition(pos);
	}
	if (event->matches(QKeySequence::SelectEndOfLine))
	{
		CursorPosition pos = CursorPosition(_cursorPosition - (_cursorPosition % (_bytesPerLine)) + (_bytesPerLine), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectStartOfLine))
	{
		CursorPosition pos = CursorPosition(_cursorPosition - (_cursorPosition % (_bytesPerLine)), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousLine))
	{
		CursorPosition pos = CursorPosition(_cursorPosition - (_bytesPerLine), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectNextLine))
	{
		CursorPosition pos = CursorPosition(_cursorPosition + (_bytesPerLine), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectNextPage))
	{
		CursorPosition pos = CursorPosition(_cursorPosition + (((viewport()->height() / _pxCharHeight) - 1) * _bytesPerLine), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousPage))
	{
		CursorPosition pos = CursorPosition(_cursorPosition - (((viewport()->height() / _pxCharHeight) - 1) * _bytesPerLine), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectEndOfDocument))
	{
		CursorPosition pos = CursorPosition(_chunks->size(), true);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectStartOfDocument))
	{
		CursorPosition pos = CursorPosition(0, true);
		setCursorPosition(pos);
		setSelection(pos);
	}

	// Edit Commands
	if (!_readOnly)
	{
		int key = int(event->text()[0].toLatin1());

		if (key >= ' ')
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				if (_overwriteMode)
				{
					qint64 len = getSelectionEnd() - getSelectionBegin();
					replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
				} else
				{
					remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
					_bPosCurrent = getSelectionBegin();
				}
				setCursorPosition(_bPosCurrent);
				resetSelection(_bPosCurrent);
			}

			// If insert mode, then insert a byte
			if (_overwriteMode == false)
				insert(_bPosCurrent, char(0));

			// Change content
			if (_chunks->size() > 0)
			{
				//if (_chunks->size() == _bPosCurrent)
				//	_chunks->insert(_bPosCurrent, 0);

				replace(_bPosCurrent, key);
				setCursorPosition(CursorPosition(_cursorPosition + 1, true));
				resetSelection(_cursorPosition);
			}
		}

		/* Cut */
		if (event->matches(QKeySequence::Cut))
		{
			QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
			for (qint64 idx = 32; idx < ba.size(); idx += 33)
				ba.insert(idx, "\n");
			QClipboard *clipboard = QApplication::clipboard();
			clipboard->setText(ba);
			if (_overwriteMode)
			{
				qint64 len = getSelectionEnd() - getSelectionBegin();
				replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
			} else
			{
				remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
			}
			CursorPosition cPos(getSelectionBegin(), true);
			setCursorPosition( cPos);
			resetSelection(cPos);
		}

		/* Paste */
		if (event->matches(QKeySequence::Paste))
		{
			QClipboard *clipboard = QApplication::clipboard();
			QByteArray ba = QByteArray().fromHex(clipboard->text().toLatin1());
			if (_overwriteMode)
				replace(_bPosCurrent, ba.size(), ba);
			else
				insert(_bPosCurrent, ba);
			setCursorPosition(CursorPosition(_cursorPosition +  ba.size(),true));
			resetSelection(CursorPosition(getSelectionBegin(),true));
		}

		/* Delete char */
		if (event->matches(QKeySequence::Delete))
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				_bPosCurrent = getSelectionBegin();
				if (_overwriteMode)
				{
					QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
					replace(_bPosCurrent, ba.size(), ba);
				} else
				{
					remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
				}
			} else
			{
				if (_overwriteMode)
					replace(_bPosCurrent, char(0));
				else
					remove(_bPosCurrent, 1);
			}
			CursorPosition cPos(_bPosCurrent, true);
			setCursorPosition(cPos);
			resetSelection(cPos);
		}

		/* Backspace */
		if ((event->key() == Qt::Key_Backspace) && (event->modifiers() == Qt::NoModifier))
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				_bPosCurrent = getSelectionBegin();
				setCursorPosition(CursorPosition(_bPosCurrent,false));
				if (_overwriteMode)
				{
					QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
					replace(_bPosCurrent, ba.size(), ba);
				} else
				{
					remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
				}
				resetSelection(CursorPosition( _bPosCurrent, true));
			} else
			{
				bool behindLastByte = false;
				if ((_cursorPosition ) == _chunks->size())
					behindLastByte = true;

				_bPosCurrent -= 1;
				if (_overwriteMode)
					replace(_bPosCurrent, char(0));
				else
					remove(_bPosCurrent, 1);

				if (!behindLastByte)
					_bPosCurrent -= 1;
				CursorPosition cPos(_bPosCurrent, true);
				setCursorPosition(cPos);
				resetSelection(cPos);
			}
		} 

		/* undo */
		if (event->matches(QKeySequence::Undo))
		{
			undo();
		}

		/* redo */
		if (event->matches(QKeySequence::Redo))
		{
			redo();
		}

	}

	/* Copy */
	if (event->matches(QKeySequence::Copy))
	{
		QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
		for (qint64 idx = 32; idx < ba.size(); idx += 33)
			ba.insert(idx, "\n");
		QClipboard *clipboard = QApplication::clipboard();
		clipboard->setText(ba);
	}

	// Switch between insert/overwrite mode
	if ((event->key() == Qt::Key_Insert) && (event->modifiers() == Qt::NoModifier))
	{
		setOverwriteMode(!overwriteMode());
		setCursorPosition(_cursorPosition);
	}

	refresh();
}
void QHexEdit::keyPressEvent(QKeyEvent *event)
{
	if (_cursorPosition.isAscii()){
		keyPressEventAscii(event);
		return;
	}
	// Cursor movements
	if (event->matches(QKeySequence::MoveToNextChar))
	{
		setCursorPosition(_cursorPosition + 1);
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousChar))
	{
		setCursorPosition(_cursorPosition - 1);
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToEndOfLine))
	{
		setCursorPosition(_cursorPosition | (2 * _bytesPerLine - 1));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToStartOfLine))
	{
		setCursorPosition(_cursorPosition - (_cursorPosition % (2 * _bytesPerLine)));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousLine))
	{
		setCursorPosition(_cursorPosition - (2 * _bytesPerLine));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToNextLine))
	{
		setCursorPosition(_cursorPosition + (2 * _bytesPerLine));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToNextPage))
	{
		setCursorPosition(_cursorPosition + (((_rowsShown - 1) * 2 * _bytesPerLine)));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToPreviousPage))
	{
		setCursorPosition(_cursorPosition - (((_rowsShown - 1) * 2 * _bytesPerLine)));
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToEndOfDocument))
	{
		setCursorPosition(_chunks->size() * 2);
		resetSelection(_cursorPosition);
	}
	if (event->matches(QKeySequence::MoveToStartOfDocument))
	{
		setCursorPosition(0);
		resetSelection(_cursorPosition);
	}

	// Select commands
	if (event->matches(QKeySequence::SelectAll))
	{
		resetSelection(0);
		setSelection(2 * _chunks->size() + 1);
	}
	if (event->matches(QKeySequence::SelectNextChar))
	{
		qint64 pos = _cursorPosition + 1;
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousChar))
	{
		qint64 pos = _cursorPosition - 1;
		setSelection(pos);
		setCursorPosition(pos);
	}
	if (event->matches(QKeySequence::SelectEndOfLine))
	{
		qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine)) + (2 * _bytesPerLine);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectStartOfLine))
	{
		qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine));
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousLine))
	{
		qint64 pos = _cursorPosition - (2 * _bytesPerLine);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectNextLine))
	{
		qint64 pos = _cursorPosition + (2 * _bytesPerLine);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectNextPage))
	{
		qint64 pos = _cursorPosition + (((viewport()->height() / _pxCharHeight) - 1) * 2 * _bytesPerLine);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectPreviousPage))
	{
		qint64 pos = _cursorPosition - (((viewport()->height() / _pxCharHeight) - 1) * 2 * _bytesPerLine);
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectEndOfDocument))
	{
		qint64 pos = _chunks->size() * 2;
		setCursorPosition(pos);
		setSelection(pos);
	}
	if (event->matches(QKeySequence::SelectStartOfDocument))
	{
		qint64 pos = 0;
		setCursorPosition(pos);
		setSelection(pos);
	}

	// Edit Commands
	if (!_readOnly)
	{
		/* Hex input */
		int key = int(event->text()[0].toLower().toLatin1()); // accept keys a-f and A-F
		if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f'))
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				if (_overwriteMode)
				{
					qint64 len = getSelectionEnd() - getSelectionBegin();
					replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
				} else
				{
					remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
					_bPosCurrent = getSelectionBegin();
				}
				setCursorPosition(2 * _bPosCurrent);
				resetSelection(2 * _bPosCurrent);
			}

			// If insert mode, then insert a byte
			if (_overwriteMode == false)
				if ((_cursorPosition % 2) == 0)
					insert(_bPosCurrent, char(0));

			// Change content
			if (_chunks->size() > 0)
			{
				//if (_chunks->size() == _bPosCurrent)
				//	_chunks->insert(_bPosCurrent, 0);
				QByteArray hexValue = _chunks->data(_bPosCurrent, 1).toHex();
				if ((_cursorPosition % 2) == 0)
					hexValue[0] = key;
				else
					hexValue[1] = key;

				replace(_bPosCurrent, QByteArray().fromHex(hexValue)[0]);

				setCursorPosition(_cursorPosition + 1);
				resetSelection(_cursorPosition);
			}
		}
		

		/* Cut */
		if (event->matches(QKeySequence::Cut))
		{
			QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
			for (qint64 idx = 32; idx < ba.size(); idx += 33)
				ba.insert(idx, "\n");
			QClipboard *clipboard = QApplication::clipboard();
			clipboard->setText(ba);
			if (_overwriteMode)
			{
				qint64 len = getSelectionEnd() - getSelectionBegin();
				replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
			} else
			{
				remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
			}
			setCursorPosition(2 * getSelectionBegin());
			resetSelection(2 * getSelectionBegin());
		}

		/* Paste */
		if (event->matches(QKeySequence::Paste))
		{
			QClipboard *clipboard = QApplication::clipboard();
			QByteArray ba = QByteArray().fromHex(clipboard->text().toLatin1());
			if (_overwriteMode)
				replace(_bPosCurrent, ba.size(), ba);
			else
				insert(_bPosCurrent, ba);
			setCursorPosition(_cursorPosition + 2 * ba.size());
			resetSelection(getSelectionBegin());
		}

		/* Delete char */
		if (event->matches(QKeySequence::Delete))
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				_bPosCurrent = getSelectionBegin();
				if (_overwriteMode)
				{
					QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
					replace(_bPosCurrent, ba.size(), ba);
				} else
				{
					remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
				}
			} else
			{
				if (_overwriteMode)
					replace(_bPosCurrent, char(0));
				else
					remove(_bPosCurrent, 1);
			}
			setCursorPosition(2 * _bPosCurrent);
			resetSelection(2 * _bPosCurrent);
		}

		/* Backspace */
		if ((event->key() == Qt::Key_Backspace) && (event->modifiers() == Qt::NoModifier))
		{
			if (getSelectionBegin() != getSelectionEnd())
			{
				_bPosCurrent = getSelectionBegin();
				setCursorPosition(2 * _bPosCurrent);
				if (_overwriteMode)
				{
					QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
					replace(_bPosCurrent, ba.size(), ba);
				} else
				{
					remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
				}
				resetSelection(2 * _bPosCurrent);
			} else
			{
				bool behindLastByte = false;
				if ((_cursorPosition / 2) == _chunks->size())
					behindLastByte = true;

				_bPosCurrent -= 1;
				if (_overwriteMode)
					replace(_bPosCurrent, char(0));
				else
					remove(_bPosCurrent, 1);

				if (!behindLastByte)
					_bPosCurrent -= 1;

				setCursorPosition(2 * _bPosCurrent);
				resetSelection(2 * _bPosCurrent);
			}
		}

		/* undo */
		if (event->matches(QKeySequence::Undo))
		{
			undo();
		}

		/* redo */
		if (event->matches(QKeySequence::Redo))
		{
			redo();
		}

	}

	/* Copy */
	if (event->matches(QKeySequence::Copy))
	{
		QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
		for (qint64 idx = 32; idx < ba.size(); idx += 33)
			ba.insert(idx, "\n");
		QClipboard *clipboard = QApplication::clipboard();
		clipboard->setText(ba);
	}

	// Switch between insert/overwrite mode
	if ((event->key() == Qt::Key_Insert) && (event->modifiers() == Qt::NoModifier))
	{
		setOverwriteMode(!overwriteMode());
		setCursorPosition(_cursorPosition);
	}

	refresh();
}

void QHexEdit::mouseMoveEvent(QMouseEvent * event)
{
    _blink = false;
    viewport()->update();
	CursorPosition cPos = cursorPosition(event->pos());
    if (cPos.pos() >= 0)
    {
        setCursorPosition(cPos);
        setSelection(cPos);
    }
}

void QHexEdit::mousePressEvent(QMouseEvent * event)
{
    _blink = false;
    viewport()->update();
	CursorPosition cPos = cursorPosition(event->pos());
    if (cPos.pos() >= 0)
    {
        resetSelection(cPos);
        setCursorPosition(cPos);
    }
}

void QHexEdit::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
	int pxOfsX = horizontalScrollBar()->value();

    if (event->rect() != _cursorRect)
    {
        int pxPosStartY = _pxCharHeight;

        // draw some patterns if needed
        painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Base));
        if (_addressArea)
            painter.fillRect(QRect(-pxOfsX, event->rect().top(), _pxPosHexX - _pxGapAdrHex/2 - pxOfsX, height()), _addressAreaColor);
        if (_asciiArea)
        {
            int linePos = _pxPosAsciiX - (_pxGapHexAscii / 2);
            painter.setPen(Qt::gray);
            painter.drawLine(linePos - pxOfsX, event->rect().top(), linePos - pxOfsX, height());
        }

        painter.setPen(viewport()->palette().color(QPalette::WindowText));

        // paint address area
        if (_addressArea)
        {
            QString address;
            for (int row=0, pxPosY = _pxCharHeight; row <= (_dataShown.size()/_bytesPerLine); row++, pxPosY +=_pxCharHeight)
            {
                address = QString("%1").arg(_bPosFirst + row*_bytesPerLine + _addressOffset, _addrDigits, 16, QChar('0'));
                painter.drawText(_pxPosAdrX - pxOfsX, pxPosY, address);
            }
        }

        // paint hex and ascii area
        QPen colStandard = QPen(viewport()->palette().color(QPalette::WindowText));

        painter.setBackgroundMode(Qt::TransparentMode);

        for (int row = 0, pxPosY = pxPosStartY; row <= _rowsShown; row++, pxPosY +=_pxCharHeight)
        {
            QByteArray hex;
            int pxPosX = _pxPosHexX  - pxOfsX;
            int pxPosAsciiX2 = _pxPosAsciiX  - pxOfsX;
            qint64 bPosLine = row * _bytesPerLine;
            for (int colIdx = 0; ((bPosLine + colIdx) < _dataShown.size() && (colIdx < _bytesPerLine)); colIdx++)
            {
                QColor c = viewport()->palette().color(QPalette::Base);
                painter.setPen(colStandard);

                qint64 posBa = _bPosFirst + bPosLine + colIdx;
                if ((getSelectionBegin() <= posBa) && (getSelectionEnd() > posBa))
                {
                    c = _brushSelection.color();
                    painter.setPen(_penSelection);
                }
                else
                {
                    if (_highlighting)
                        if (_markedShown.at((int)(posBa - _bPosFirst)))
                        {
                            c = _brushHighlighted.color();
                            painter.setPen(_penHighlighted);
                        }
                }

                // render hex value
                QRect r;
                if (colIdx == 0)
                    r.setRect(pxPosX, pxPosY - _pxCharHeight + _pxSelectionSub, 2*_pxCharWidth, _pxCharHeight);
                else
                    r.setRect(pxPosX - _pxCharWidth, pxPosY - _pxCharHeight + _pxSelectionSub, 3*_pxCharWidth, _pxCharHeight);
                painter.fillRect(r, c);
                hex = _hexDataShown.mid((bPosLine + colIdx) * 2, 2);
                painter.drawText(pxPosX, pxPosY, hex);
                pxPosX += 3*_pxCharWidth;

                // render ascii value
                if (_asciiArea)
                {
                    char ch = _dataShown.at(bPosLine + colIdx);
                    if (ch < 0x20 )
                            ch = '.';
                    r.setRect(pxPosAsciiX2, pxPosY - _pxCharHeight + _pxSelectionSub, _pxCharWidth, _pxCharHeight);
                    painter.fillRect(r, c);
                    painter.drawText(pxPosAsciiX2, pxPosY, QChar(ch));
                    pxPosAsciiX2 += _pxCharWidth;
                }
            }
        }
        painter.setBackgroundMode(Qt::TransparentMode);
        painter.setPen(viewport()->palette().color(QPalette::WindowText));
    }

    // paint cursor
    if (_blink && !_readOnly && hasFocus())
        painter.fillRect(_cursorRect, this->palette().color(QPalette::WindowText));
	else{
		QRect r(_pxCursorX - pxOfsX, _pxCursorY - _pxCharHeight, _pxCharWidth, _pxCharHeight);
		QColor c(viewport()->palette().color(QPalette::Base));
		painter.fillRect(r, c);
		if (_cursorPosition.isAscii())
			painter.drawText(_pxCursorX - pxOfsX, _pxCursorY, QChar(_dataShown.at(_cursorPosition - _bPosFirst)));
		else
			painter.drawText(_pxCursorX - pxOfsX, _pxCursorY, _hexDataShown.mid(_cursorPosition - _bPosFirst * 2, 1));
	}

    // emit event, if size has changed
    if (_lastEventSize != _chunks->size())
    {
        _lastEventSize = _chunks->size();
        emit currentSizeChanged(_lastEventSize);
    }
}

void QHexEdit::resizeEvent(QResizeEvent *)
{
    adjust();
}

// ********************************************************************** Handle selections
void QHexEdit::resetSelection()
{
    _bSelectionBegin = _bSelectionInit;
    _bSelectionEnd = _bSelectionInit;
}

void QHexEdit::resetSelection(QHexEdit::CursorPosition pos)
{
	if (pos.isAscii() == false)
		pos = pos / 2;
    if (pos < 0)
        pos = 0;
    if (pos > _chunks->size())
        pos = _chunks->size();

    _bSelectionInit = pos;
    _bSelectionBegin = pos;
    _bSelectionEnd = pos;
}

void QHexEdit::setSelection(QHexEdit::CursorPosition pos)
{
	if (pos.isAscii() == false)
		pos = pos / 2;
    if (pos < 0)
        pos = 0;
    if (pos > _chunks->size())
        pos = _chunks->size();

    if (pos >= _bSelectionInit)
    {
        _bSelectionEnd = pos;
        _bSelectionBegin = _bSelectionInit;
    }
    else
    {
        _bSelectionBegin = pos;
        _bSelectionEnd = _bSelectionInit;
    }
}

int QHexEdit::getSelectionBegin()
{
    return _bSelectionBegin;
}

int QHexEdit::getSelectionEnd()
{
    return _bSelectionEnd;
}

// ********************************************************************** Private utility functions
void QHexEdit::init()
{
    _undoStack->clear();
    setAddressOffset(0);
    resetSelection(0);
    setCursorPosition(0);
    verticalScrollBar()->setValue(0);
    _modified = false;
}

void QHexEdit::adjust()
{
    // recalc Graphics
    _pxPosHexX = _pxGapAdrHex;
    if (_addressArea)
    {
        _addrDigits = addressWidth();
        _pxPosHexX += _pxGapAdr + _addrDigits*_pxCharWidth;
    }

    _pxPosAdrX = _pxGapAdr;
    _pxPosAsciiX = _pxPosHexX + _hexCharsInLine * _pxCharWidth + _pxGapHexAscii;

    // set horizontalScrollBar()
    int pxWidth = _pxPosAsciiX;
    if (_asciiArea)
        pxWidth += _bytesPerLine*_pxCharWidth;
    horizontalScrollBar()->setRange(0, pxWidth - viewport()->width());
    horizontalScrollBar()->setPageStep(viewport()->width());

    // set verticalScrollbar()
    _rowsShown = ((viewport()->height()-4)/_pxCharHeight);
    int lineCount = (int)(_chunks->size() / (qint64)_bytesPerLine) + 1;
    verticalScrollBar()->setRange(0, lineCount - _rowsShown);
    verticalScrollBar()->setPageStep(_rowsShown);

    int value = verticalScrollBar()->value();
    _bPosFirst = (qint64)value * _bytesPerLine;
    _bPosLast = _bPosFirst + (qint64)(_rowsShown * _bytesPerLine) - 1;
    if (_bPosLast >= _chunks->size())
        _bPosLast = _chunks->size() - 1;
    readBuffers();
    setCursorPosition(_cursorPosition);
}

void QHexEdit::dataChangedPrivate(int)
{
    _modified = _undoStack->index() != 0;
    adjust();
    emit dataChanged();
}

void QHexEdit::refresh()
{
    ensureVisible();
    readBuffers();
}

void QHexEdit::readBuffers()
{
    _dataShown = _chunks->data(_bPosFirst, _bPosLast - _bPosFirst + _bytesPerLine + 1, &_markedShown);
    _hexDataShown = QByteArray(_dataShown.toHex());
}

QString QHexEdit::toReadable(const QByteArray &ba)
{
    QString result;

    for (int i=0; i < ba.size(); i += 16)
    {
        QString addrStr = QString("%1").arg(_addressOffset + i, addressWidth(), 16, QChar('0'));
        QString hexStr;
        QString ascStr;
        for (int j=0; j<16; j++)
        {
            if ((i + j) < ba.size())
            {
                hexStr.append(" ").append(ba.mid(i+j, 1).toHex());
                char ch = ba[i + j];
                if ((ch < 0x20) || (ch > 0x7e))
                        ch = '.';
                ascStr.append(QChar(ch));
            }
        }
        result += addrStr + " " + QString("%1").arg(hexStr, -48) + "  " + QString("%1").arg(ascStr, -17) + "\n";
    }
    return result;
}

void QHexEdit::updateCursor()
{
    if (_blink)
        _blink = false;
    else
        _blink = true;
    viewport()->update(_cursorRect);
}

