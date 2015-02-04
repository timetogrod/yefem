#include <QPainter>
#include <QStyleOptionViewItem>
#include <QKeySequence>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QScrollBar>
#include <QClipboard>
#include <QFileInfo>
#include <QDrag>
#include <QDebug>

#include "yefswidget.h"
#include "yefshandler.h"
#include "yefsactions.h"

#include "yefslistview.h"
#include "yefslistmodel.h"
#include "yefsmodel.h"
#include "yefilepane.h"

#include "yemainwindow.h"
#include "yeapplication.h"
#include "yeappresources.h"
#include "yeappdata.h"
#include "yeapp.h"

#include "yemime.h"
#include "yefileutils.h"
#include "yetreenode.h"
//==============================================================================================================================
#define SP_ROOT_EX 4

FsListDelegate::FsListDelegate(FsListView *view)
	: QItemDelegate(view)
	, m_view(view)
{
}

void FsListDelegate::updateRowHeight()
{
	m_rowHeight = R::data().iconSize + 2;
}

QSize FsListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index ) const
{
	QSize size = QItemDelegate::sizeHint(option, index);
	return QSize(size.width(), m_rowHeight);
}

void FsListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QItemDelegate::paint(painter, option, index);

/*	if (m_view->m_hoverIndex == index) {
		QRect o = option.rect;
		QRect r = QRect(o.x(), o.y(), o.height() + SP_ROOT_EX, o.height() - 1);
	//	qDebug() << "FsListDelegate::paint" << r;

		if (m_view->m_hoverState == FsListViewHoverState::Icon) {
			QColor bg(255, 0, 168, 40);
			QBrush brush(bg);
		//	painter->setBrush(brush);
			QColor c = QColor(255, 0, 168, 192);
			painter->setPen(c);
			painter->drawRect(r);
			painter->fillRect(r, brush);
		}
		else {
			QColor bg(0, 160, 255, 40);
			QBrush brush(bg);
		//	painter->setBrush(brush);
			QColor c = QColor(0, 160, 255, 192);
			painter->setPen(c);
			painter->drawRect(r);
			painter->fillRect(r, brush);
		}
	} */
}

//==============================================================================================================================
// class FsListView
//==============================================================================================================================

FsListView::FsListView(FsWidget *widget, QWidget *parent)
	: QListView(parent)
	, m_widget(widget)
	, m_handler(widget->handler())
	, m_inited(false)
	, m_clickEnter(R::data().clickEnter)
	, m_hoverArea(FsHoverArea::None)
{
	setWrapping(true);
	setResizeMode(QListView::Adjust);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setSelectionRectVisible(true);
	setEditTriggers(QAbstractItemView::EditKeyPressed /*| QAbstractItemView::SelectedClicked*/); // handle click-to-sel ourself
	setDefaultDropAction(Qt::MoveAction);
	setDragDropMode(QAbstractItemView::DragDrop);
	setDropIndicatorShown(true);
	setMouseTracking(true);

	m_delegate = new FsListDelegate(this);
	setItemDelegate(m_delegate);
}

FsListView::~FsListView()
{
}

void FsListView::lateStart()
{
	updateIconTheme();	// after: setItemDelegate()
	updateSettings();

	connect(this, SIGNAL(entered(QModelIndex)), m_handler, SLOT(onItemHovered(QModelIndex)));
}

void FsListView::updateSettings()
{
	if (m_inited) {
		if (R::data().clickEnter == m_clickEnter) return;
		if (m_clickEnter == ClickEnter::DoubleClick) {
			disconnect(this, SIGNAL(doubleClicked(QModelIndex)), m_handler, SLOT(onItemActivated(QModelIndex)));
		} else {
			disconnect(this, SIGNAL(clicked(QModelIndex)), m_handler, SLOT(onItemActivated(QModelIndex)));
		}
	}

	m_inited = true;
	m_clickEnter = R::data().clickEnter;

	if (m_clickEnter == ClickEnter::DoubleClick) {
		connect(this, SIGNAL(doubleClicked(QModelIndex)), m_handler, SLOT(onItemActivated(QModelIndex)));
	} else {
		connect(this, SIGNAL(clicked(QModelIndex)), m_handler, SLOT(onItemActivated(QModelIndex)));
	}

	if (m_clickEnter != ClickEnter::SingleClick) clearHoverState();
}

void FsListView::updateIconTheme()
{
	m_delegate->updateRowHeight();

	int sz = R::data().iconSize;
	setIconSize(QSize(sz, sz));
}

void FsListView::updateCursor(Qt::CursorShape cursor)
{
	if (m_cursor != cursor) {
		m_cursor = cursor;
		setCursor(cursor);
	}
}

bool FsListView::canHoverSelect(bool isDir)
{
	bool ok = (m_clickEnter == ClickEnter::SingleClick) && isDir;
	updateCursor(ok ? Qt::PointingHandCursor : Qt::ArrowCursor);
	return ok;
}

void FsListView::clearHoverState()
{
	m_handler->stopHoverSelect();
	updateCursor(Qt::ArrowCursor);
}
//==============================================================================================================================

void FsListView::execRename()
{
	QModelIndex index = currentIndex();
	if (index.isValid()) edit(index);
}
//==============================================================================================================================

bool FsListView::viewportEvent(QEvent *event)
{
	if (m_clickEnter == ClickEnter::SingleClick && event->type() == QEvent::Leave) {
		clearHoverState();
	}
	return QListView::viewportEvent(event);
}

void FsListView::mouseMoveEvent(QMouseEvent *event)
{
	QListView::mouseMoveEvent(event);

	switch (m_clickEnter) {
		case ClickEnter::DoubleClick: break;
		case ClickEnter::SingleClick:
			if (m_cursor != Qt::ArrowCursor && event->button() == Qt::NoButton) {
				bool keyStop = R::data().keyStopHover && QApplication::keyboardModifiers() != Qt::NoModifier;
				if (keyStop || !indexAt(event->pos()).isValid()) clearHoverState();
			}
			break;
		case ClickEnter::ClickIcon:
			m_hoverArea = FsHoverArea::None;
			if (event->button() == Qt::NoButton) {
				QModelIndex index = indexAt(event->pos());
				if (index.isValid()) {
					QRect r = visualRect(index);
					int x0 = r.x() + 4;
					int y0 = r.y() + 1;
					int x1 = x0 + 16;
					int y1 = y0 + 16;
					int x = event->pos().x();
					int y = event->pos().y();
					m_hoverArea = (x >= x0 && x <= x1 && y >= y0 && y <= y1) ? FsHoverArea::Icon : FsHoverArea::Text;
				//	qDebug() << event->pos() << x0 << x1 << y0 << y1 << r;
				}
			}
			updateCursor(m_hoverArea == FsHoverArea::Icon ? Qt::PointingHandCursor : Qt::ArrowCursor);
			break;
	}
}

void FsListView::wheelEvent(QWheelEvent *event)
{
	QScrollBar *scroll = horizontalScrollBar();
	if (scroll) {
		QApplication::sendEvent(scroll, event);
		event->accept();
	}
}

void FsListView::keyPressEvent(QKeyEvent *event)
{
	m_handler->handleKeyPress(event);
}

void FsListView::contextMenuEvent(QContextMenuEvent *event)
{
	m_handler->showContextMenu(event);
}
//==============================================================================================================================
/*
void FsListView::dragEnterEvent(QDragEnterEvent *event)
{
//	qDebug() << "FsListView::dragEnterEvent" << m_pressed;
	event->accept();
	QListView::dragEnterEvent(event);
//	qDebug() << "FsListView::dragEnterEvent.2";
}

void FsListView::dragLeaveEvent(QDragLeaveEvent *event)
{
//	qDebug() << "FsListView::dragLeaveEvent.1" << m_pressed;
	QListView::dragLeaveEvent(event);
	event->accept();
//	qDebug() << "FsListView::dragLeaveEvent.2" << m_pressed;
}

void FsListView::dragMoveEvent(QDragMoveEvent *event)
{
	QListView::dragMoveEvent(event);
	event->accept();
//	qDebug() << "FsListView::dragMoveEvent";
}

void FsListView::dropEvent(QDropEvent *event)
{
//	qDebug() << "FsListView::dropEvent.1" << m_pressed;
	m_model->setDropWidget(m_widget);
	QListView::dropEvent(event);
	event->accept();
//	resetCursor();
//	qDebug() << "FsListView::dropEvent.2" << m_pressed;
}*/
//==============================================================================================================================

void FsListView::startDrag(Qt::DropActions supportedActions)
{
//  Q_D(QAbstractItemView);
	QModelIndexList indexes = this->selectedIndexes();// d->selectedDraggableIndexes();
	if (indexes.count() > 0) {
		QMimeData *data = model()->mimeData(indexes);
		if (!data)
			return;

		QRect rect;
		//QPixmap pixmap = d->renderToPixmap(indexes, &rect);
		rect.adjust(horizontalOffset(), verticalOffset(), 0, 0);
		QDrag *drag = new QDrag(this);
		//drag->setPixmap(pixmap);
		drag->setMimeData(data);
		drag->setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) - rect.topLeft());

		Qt::DropAction defaultDropAction = Qt::IgnoreAction;
		if (this->defaultDropAction() != Qt::IgnoreAction && (supportedActions & this->defaultDropAction()))
			defaultDropAction = this->defaultDropAction();
		else if (supportedActions & Qt::CopyAction && dragDropMode() != QAbstractItemView::InternalMove)
			defaultDropAction = Qt::CopyAction;

		if (drag->exec(supportedActions, defaultDropAction) == Qt::MoveAction) {
		//	clearOrRemove();
		}
	}
}

void FsListView::clearOrRemove()
{
	const QItemSelection selection = selectionModel()->selection();
	QList<QItemSelectionRange>::const_iterator it = selection.constBegin();

	if (!dragDropOverwriteMode()) {
		for (; it != selection.constEnd(); ++it) {
			QModelIndex parent = (*it).parent();

			if ((*it).left() != 0) {
				continue;
			}

			if ((*it).right() != (model()->columnCount(parent) - 1)) {
				continue;
			}

			int count = (*it).bottom() - (*it).top() + 1;
			model()->removeRows((*it).top(), count, parent);
		}
	}
	else {
		// we can't remove the rows so reset the items (i.e. the view is like a table)
		QModelIndexList list = selection.indexes();

		for (int i = 0; i < list.size(); ++i) {
			QModelIndex index = list.at(i);
			QMap<int, QVariant> roles = model()->itemData(index);

			for (QMap<int, QVariant>::Iterator it = roles.begin(); it != roles.end(); ++it) {
				it.value() = QVariant();
			}

			model()->setItemData(index, roles);
		}
	}
}
//==============================================================================================================================
