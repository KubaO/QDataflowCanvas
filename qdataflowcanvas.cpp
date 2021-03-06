/* QDataflowCanvas - a dataflow widget for Qt
 * Copyright (C) 2017 Federico Ferri
 * Copyright (C) 2018 Kuba Ober
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "qdataflowcanvas.h"
#include "utility.h"

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>

#include <QtMath>

#include <QDebug>

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QApplication>
#include <QTextCursor>
#include <QTextDocument>

QDataflowCanvas::QDataflowCanvas(QWidget *parent)
    : QGraphicsView(parent), model_()
{
    QGraphicsScene *scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene->setSceneRect(0, 0, 200, 200);
    setScene(scene);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::TextAntialiasing, true);
    setTransformationAnchor(AnchorUnderMouse);
    scale(0.75, 0.75);
    setMinimumSize(200, 200);

    QRadialGradient gradient(0, 0, 800);
    gradient.setColorAt(0, QColor(240,240,240));
    gradient.setColorAt(1, QColor(160,160,160));
    setBackgroundBrush(gradient);

    completion_ = new QDataflowTextCompletion();

    setDragMode(QGraphicsView::RubberBandDrag);

    setModel(new QDataflowModel(this));

    showObjectHoverFeedback_ = false;
    showConnectionHoverFeedback_ = false;
    showIOletsTooltips_ = false;
    gridSize_ = 1.0;
    drawGrid_ = false;
}

QDataflowCanvas::~QDataflowCanvas()
{
    scene()->clearSelection();

    for (auto *node : as_const(ownedNodes_))
        delete node;

    for (auto *conn : as_const(ownedConnections_))
        delete conn;
}

QDataflowModel * QDataflowCanvas::model()
{
    return model_;
}

void QDataflowCanvas::setModel(QDataflowModel *model)
{
    if(model_)
    {
        QObject::disconnect(model_, &QDataflowModel::nodeAdded, this, &QDataflowCanvas::onNodeAdded);
        QObject::disconnect(model_, &QDataflowModel::nodeRemoved, this, &QDataflowCanvas::onNodeRemoved);
        QObject::disconnect(model_, &QDataflowModel::nodeValidChanged, this, &QDataflowCanvas::onNodeValidChanged);
        QObject::disconnect(model_, &QDataflowModel::nodePosChanged, this, &QDataflowCanvas::onNodePosChanged);
        QObject::disconnect(model_, &QDataflowModel::nodeTextChanged, this, &QDataflowCanvas::onNodeTextChanged);
        QObject::disconnect(model_, &QDataflowModel::nodeInletCountChanged, this, &QDataflowCanvas::onNodeInletCountChanged);
        QObject::disconnect(model_, &QDataflowModel::nodeOutletCountChanged, this, &QDataflowCanvas::onNodeOutletCountChanged);
        QObject::disconnect(model_, &QDataflowModel::connectionAdded, this, &QDataflowCanvas::onConnectionAdded);
        QObject::disconnect(model_, &QDataflowModel::connectionRemoved, this, &QDataflowCanvas::onConnectionRemoved);
        model_->deleteLater();
    }

    model_ = model;
    model_->setParent(this);
    QObject::connect(model_, &QDataflowModel::nodeAdded, this, &QDataflowCanvas::onNodeAdded);
    QObject::connect(model_, &QDataflowModel::nodeRemoved, this, &QDataflowCanvas::onNodeRemoved);
    QObject::connect(model_, &QDataflowModel::nodeValidChanged, this, &QDataflowCanvas::onNodeValidChanged);
    QObject::connect(model_, &QDataflowModel::nodePosChanged, this, &QDataflowCanvas::onNodePosChanged);
    QObject::connect(model_, &QDataflowModel::nodeTextChanged, this, &QDataflowCanvas::onNodeTextChanged);
    QObject::connect(model_, &QDataflowModel::nodeInletCountChanged, this, &QDataflowCanvas::onNodeInletCountChanged);
    QObject::connect(model_, &QDataflowModel::nodeOutletCountChanged, this, &QDataflowCanvas::onNodeOutletCountChanged);
    QObject::connect(model_, &QDataflowModel::connectionAdded, this, &QDataflowCanvas::onConnectionAdded);
    QObject::connect(model_, &QDataflowModel::connectionRemoved, this, &QDataflowCanvas::onConnectionRemoved);
}

QList<QDataflowNode*> QDataflowCanvas::selectedNodes()
{
    auto const isSelected = [this](QDataflowNode *node){
        return node->scene() == scene() && node->isSelected();
    };
    QList<QDataflowNode*> ret;
    std::copy_if(nodes_.begin(), nodes_.end(), std::back_inserter(ret), isSelected);
    return ret;
}

QList<QDataflowConnection*> QDataflowCanvas::selectedConnections()
{
    auto const isSelected = [this](QDataflowConnection *conn){
        return conn->scene() == scene() && conn->isSelected();
    };
    QList<QDataflowConnection*> ret;
    std::copy_if(connections_.begin(), connections_.end(), std::back_inserter(ret), isSelected);
    return ret;
}

bool QDataflowCanvas::isSomeNodeInEditMode() const
{
    auto const inEditMode = [this](QDataflowNode *node){
        return node->scene() == scene() && node->isInEditMode();
    };
    return std::any_of(nodes_.begin(), nodes_.end(), inEditMode);
}

QDataflowNode * QDataflowCanvas::node(QDataflowModelNode *node)
{
    auto it = nodes_.find(node);
    if(it == nodes_.end())
    {
        qWarning() << this << "does not know about" << node;
        return {};
    }
    return *it;
}

QDataflowConnection * QDataflowCanvas::connection(QDataflowModelConnection *conn)
{
    QMap<QDataflowModelConnection*, QDataflowConnection*>::Iterator it = connections_.find(conn);
    if(it == connections_.end())
    {
        qDebug() << "WARNING:" << this << "does not know about" << conn;
        return {};
    }
    return *it;
}

void QDataflowCanvas::raiseItem(QGraphicsItem *item)
{
    qreal maxZ = 0;
    for(auto *item1 : as_const(item->collidingItems(Qt::IntersectsItemBoundingRect)))
        maxZ = qMax(maxZ, item1->zValue());
    item->setZValue(maxZ + 1);

    if(QDataflowNode *node = dynamic_cast<QDataflowNode*>(item))
    {
        for(int i = 0; i < node->inletCount(); i++)
        {
            QDataflowInlet *inlet = node->inlet(i);
            for(auto *conn : as_const(inlet->connections()))
            {
                raiseItem(conn);
            }
        }
        for(int i = 0; i < node->outletCount(); i++)
        {
            QDataflowOutlet *outlet = node->outlet(i);
            for(auto *conn : as_const(outlet->connections()))
            {
                raiseItem(conn);
            }
        }
    }
}

bool QDataflowCanvas::showIOletTooltips()
{
    return showIOletsTooltips_;
}

void QDataflowCanvas::setShowIOletTooltips(bool show)
{
    showIOletsTooltips_ = show;

    if(show)
    {
        if(showObjectHoverFeedback())
            setShowObjectHoverFeedback(false);
        if(showConnectionHoverFeedback())
            setShowConnectionHoverFeedback(false);
    }
}

bool QDataflowCanvas::showObjectHoverFeedback()
{
    return showObjectHoverFeedback_;
}

void QDataflowCanvas::setShowObjectHoverFeedback(bool show)
{
    showObjectHoverFeedback_ = show;

    if(show)
    {
        if(showIOletTooltips())
            setShowIOletTooltips(false);
    }

    for (auto *node : as_const(nodes_))
    {
        node->setAcceptHoverEvents(show);
    }
}

bool QDataflowCanvas::showConnectionHoverFeedback()
{
    return showConnectionHoverFeedback_;
}

void QDataflowCanvas::setShowConnectionHoverFeedback(bool show)
{
    showConnectionHoverFeedback_ = show;

    if(show)
    {
        if(showIOletTooltips())
            setShowIOletTooltips(false);
    }

    for(auto *conn : as_const(connections_))
    {
        conn->setAcceptHoverEvents(show);
    }
}

qreal QDataflowCanvas::gridSize()
{
    return gridSize_;
}

void QDataflowCanvas::setGridSize(qreal sz)
{
    gridSize_ = qMax(1.0, sz);
}

bool QDataflowCanvas::drawGrid()
{
    return drawGrid_;
}

void QDataflowCanvas::setDrawGrid(bool draw)
{
    drawGrid_ = draw;
}

void QDataflowCanvas::drawBackground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawBackground(painter, rect);

    if(drawGrid_)
    {
        painter->setPen(QPen(Qt::gray));
        for(qreal y = qCeil(rect.top() / gridSize_) * gridSize_; y <= rect.bottom(); y += gridSize_)
        {
            for(qreal x = qCeil(rect.left() / gridSize_) * gridSize_; x <= rect.right(); x += gridSize_)
            {
                painter->drawPoint(int(x), int(y));
            }
        }
    }
}

void QDataflowCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    QGraphicsItem *item = itemAt(event->pos());
    if(!item)
    {
        QPoint pos(mapToScene(event->pos()).toPoint());
        model_->create(pos, "", 0, 0);
        event->accept();
        return;
    }
    event->ignore();
    QGraphicsView::mouseDoubleClickEvent(event);
}

void QDataflowCanvas::keyPressEvent(QKeyEvent *event)
{
    event->ignore();

    if(event->key() == Qt::Key_Backspace && !isSomeNodeInEditMode())
    {
        for(auto *conn : as_const(selectedConnections()))
            model()->disconnect(
                        conn->source()->node()->modelNode(), conn->source()->index(),
                        conn->dest()->node()->modelNode(), conn->dest()->index()
                        );
        for(auto *node : as_const(selectedNodes()))
            model()->remove(node->modelNode());
        event->accept();
    }

    QGraphicsView::keyPressEvent(event);
}

void QDataflowCanvas::itemTextEditorTextChange()
{
    QObject *senderParent = sender()->parent();
    if(!senderParent) return;
    QObject *senderGrandParent = senderParent->parent();
    if(!senderGrandParent) return;
    QDataflowNodeTextLabel *txtItem = dynamic_cast<QDataflowNodeTextLabel*>(senderGrandParent);
    if(!txtItem) return;
    QGraphicsItem *item = txtItem->topLevelItem();
    if(!item) return;
    QDataflowNode *node = dynamic_cast<QDataflowNode*>(item);
    if(!node) return;
    node->adjust();
    txtItem->complete();
}

void QDataflowCanvas::onNodeAdded(QDataflowModelNode *mdlnode)
{
    QDataflowNode *uinode = new QDataflowNode(this, mdlnode);
    nodes_[mdlnode] = uinode;
    scene()->addItem(uinode);

    if(mdlnode->text() == "")
    {
        uinode->enterEditMode();
    }
}

void QDataflowCanvas::onNodeRemoved(QDataflowModelNode *mdlnode)
{
    QDataflowNode *uinode = node(mdlnode);
    if(uinode->isInEditMode())
        uinode->exitEditMode(true);
    scene()->removeItem(uinode);
}

void QDataflowCanvas::onNodeValidChanged(QDataflowModelNode *mdlnode, bool valid)
{
    QDataflowNode *uinode = node(mdlnode);
    uinode->setValid(valid);
}

void QDataflowCanvas::onNodePosChanged(QDataflowModelNode *mdlnode, const QPoint &pos)
{
    QDataflowNode *uinode = node(mdlnode);
    if(uinode)
    {
        uinode->setFlag(QGraphicsItem::ItemSendsGeometryChanges, false);
        uinode->setPos(pos);
        uinode->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }
}

void QDataflowCanvas::onNodeTextChanged(QDataflowModelNode *mdlnode, const QString &text)
{
    QDataflowNode *uinode = node(mdlnode);
    uinode->setText(text);
}

void QDataflowCanvas::onNodeInletCountChanged(QDataflowModelNode *mdlnode, int count)
{
    QDataflowNode *uinode = node(mdlnode);
    uinode->setInletCount(count);
}

void QDataflowCanvas::onNodeOutletCountChanged(QDataflowModelNode *mdlnode, int count)
{
    QDataflowNode *uinode = node(mdlnode);
    uinode->setOutletCount(count);
}

void QDataflowCanvas::onConnectionAdded(QDataflowModelConnection *mdlconn)
{
    QDataflowConnection *uiconn = new QDataflowConnection(this, mdlconn);
    connections_[mdlconn] = uiconn;
    scene()->addItem(uiconn);
    raiseItem(uiconn);
}

void QDataflowCanvas::onConnectionRemoved(QDataflowModelConnection *mdlconn)
{
    QDataflowConnection *uiconn = connection(mdlconn);
    scene()->removeItem(uiconn);
}

QDataflowNode::QDataflowNode(QDataflowCanvas *canvas, QDataflowModelNode *modelNode)
    : canvas_(canvas), modelNode_(modelNode), valid_(true)
{
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(canvas_->showObjectHoverFeedback());
    setCacheMode(DeviceCoordinateCache);

#if 0
    QGraphicsDropShadowEffect *shadowFx = new QGraphicsDropShadowEffect();
    shadowFx->setBlurRadius(2 * ioletHeight());
    shadowFx->setXOffset(ioletHeight());
    shadowFx->setYOffset(ioletHeight());
    setGraphicsEffect(shadowFx);
#endif

    inputHeader_ = new QGraphicsRectItem(this);

    objectBox_ = new QGraphicsRectItem(this);

    outputHeader_ = new QGraphicsRectItem(this);

    textItem_ = new QDataflowNodeTextLabel(this, objectBox_);
    textItem_->document()->setPlainText(modelNode->text());

    QObject::connect(textItem_->document(), &QTextDocument::contentsChanged, canvas, &QDataflowCanvas::itemTextEditorTextChange);

    setAcceptTouchEvents(false);
    inputHeader_->setAcceptTouchEvents(false);
    objectBox_->setAcceptTouchEvents(false);
    outputHeader_->setAcceptTouchEvents(false);
    textItem_->setAcceptTouchEvents(false);

    setInletCount(modelNode->inletCount(), true);
    setOutletCount(modelNode->outletCount(), true);

    adjust();

    setPos(modelNode->pos().x(), modelNode->pos().y());
}

QDataflowModelNode * QDataflowNode::modelNode() const
{
    return modelNode_;
}

void QDataflowNode::setInletCount(int count, bool skipAdjust)
{
    while(inlets_.length() > count)
    {
        QDataflowInlet *lastInlet = inlets_.back();
        for(auto *conn : as_const(lastInlet->connections()))
            canvas()->scene()->removeItem(conn);
        canvas()->scene()->removeItem(lastInlet);
        inlets_.pop_back();
        delete lastInlet;
    }

    while(inlets_.length() < count)
    {
        int i = inlets_.length();
        QDataflowInlet *inlet = new QDataflowInlet(this, i);
        inlet->setParentItem(inputHeader_);
        inlet->setAcceptTouchEvents(false);
        inlet->setPos(ioletWidth() / 2 + i * (ioletWidth() + ioletSpacing()), ioletHeight() / 2);
        inlets_.push_back(inlet);
    }

    if(!skipAdjust)
        adjust();
}

void QDataflowNode::setOutletCount(int count, bool skipAdjust)
{
    while(outlets_.length() > count)
    {
        QDataflowOutlet *lastOutlet = outlets_.back();
        for(auto *conn : as_const(lastOutlet->connections()))
            canvas()->scene()->removeItem(conn);
        canvas()->scene()->removeItem(lastOutlet);
        outlets_.pop_back();
        delete lastOutlet;
    }

    while(outlets_.length() < count)
    {
        int i = outlets_.length();
        QDataflowOutlet *outlet = new QDataflowOutlet(this, i);
        outlet->setParentItem(outputHeader_);
        outlet->setAcceptTouchEvents(false);
        outlet->setPos(ioletWidth() / 2 + i * (ioletWidth() + ioletSpacing()), ioletHeight() / 2);
        outlets_.push_back(outlet);
    }

    if(!skipAdjust)
        adjust();
}

void QDataflowNode::setText(const QString &text)
{
    if(text == this->text()) return;

    textItem_->setPlainText(text);
}

QString QDataflowNode::text() const
{
    return textItem_->document()->toPlainText();
}

void QDataflowNode::setValid(bool valid)
{
    valid_ = valid;

    inputHeader_->setVisible(valid);
    outputHeader_->setVisible(valid);

    adjust();
}

bool QDataflowNode::isValid() const
{
    return valid_;
}

void QDataflowNode::adjustConnections() const
{
    for(auto *inlet : inlets_)
    {
        inlet->adjustConnections();
    }

    for(auto *outlet : outlets_)
    {
        outlet->adjustConnections();
    }
}

QRectF QDataflowNode::boundingRect() const
{
    QRectF r = objectBox_->boundingRect();
    r.setHeight(r.height() + 2 * ioletHeight());
    qreal adj = ioletHeight();
    r.adjust(-adj, -adj, adj, adj);
    return r;
}

void QDataflowNode::adjust()
{
    QRectF r = textItem_->boundingRect();
    qreal w = std::max(r.width(), std::max(inletsWidth(), outletsWidth()));

    prepareGeometryChange();

    inputHeader_->setPos(0, 0);
    objectBox_->setPos(0, ioletHeight());
    outputHeader_->setPos(0, ioletHeight() + r.height());

    inputHeader_->setRect(0, 0, w, ioletHeight());
    objectBox_->setRect(0, 0, w, r.height());
    outputHeader_->setRect(0, 0, w, ioletHeight());

    QPen pen = objectPen();
    inputHeader_->setPen(pen);
    objectBox_->setPen(pen);
    outputHeader_->setPen(pen);

    QBrush ob = objectBrush(), hb = headerBrush();

    objectBox_->setBrush(ob);
    outputHeader_->setBrush(hb);
    inputHeader_->setBrush(hb);

    textItem_->setDefaultTextColor(pen.color());

    inputHeader_->setVisible(isValid());
    outputHeader_->setVisible(isValid());

    adjustConnections();
}

qreal QDataflowNode::inletsWidth() const
{
    return inletCount() * (ioletWidth() + ioletSpacing()) - ioletSpacing();
}

qreal QDataflowNode::outletsWidth() const
{
    return outletCount() * (ioletWidth() + ioletSpacing()) - ioletSpacing();
}

QPen QDataflowNode::objectPen() const
{
    return QPen(isSelected() ? Qt::blue : Qt::black, 1, isValid() ? Qt::SolidLine : Qt::DashLine);
}

QBrush QDataflowNode::objectBrush() const
{
    return Qt::white;
}

QBrush QDataflowNode::headerBrush() const
{
    return Qt::lightGray;
}

QPen QDataflowNode::tempConnectionPen() const
{
    return QPen(Qt::gray, 1, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
}

QPen QDataflowNode::connectionPen() const
{
    return QPen(Qt::black, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QPen QDataflowNode::invalidConnectionPen() const
{
    return QPen(Qt::red, 1, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
}

void QDataflowNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    bool sel = option->state & QStyle::State_Selected,
            hov = canvas()->showObjectHoverFeedback() &&
                option->state & QStyle::State_MouseOver;

    if(sel || hov)
    {
        painter->fillRect(boundingRect(), sel ? Qt::cyan : Qt::gray);
    }
}

void QDataflowNode::enterEditMode()
{
    oldText_ = text();
    setSelected(true);
    textItem_->setFlag(QGraphicsItem::ItemIsFocusable, true);
    //textItem_->setTextInteractionFlags(Qt::TextEditable);
    textItem_->setTextInteractionFlags(Qt::TextEditorInteraction);
    textItem_->setFocus();
    QTextCursor cursor = textItem_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.select(QTextCursor::Document);
    textItem_->setTextCursor(cursor);
    textItem_->complete();
}

void QDataflowNode::exitEditMode(bool revertText)
{
    textItem_->clearCompletion();
    if(revertText)
        textItem_->setPlainText(oldText_);
    else if(oldText_ != text())
    {
        modelNode_->setText(text());
        oldText_ = text();
    }
    textItem_->clearFocus();
    QTextCursor cursor = textItem_->textCursor();
    cursor.clearSelection();
    textItem_->setTextCursor(cursor);
    textItem_->setFlag(QGraphicsItem::ItemIsFocusable, false);
    textItem_->setTextInteractionFlags(Qt::NoTextInteraction);
}

bool QDataflowNode::isInEditMode() const
{
    if(!textItem_) return false;
    return (textItem_->textInteractionFlags() & Qt::TextEditable) && textItem_->hasFocus();
}

QVariant QDataflowNode::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionHasChanged:
        if(canvas()->gridSize() > 1)
        {
            const qreal gridSize = canvas()->gridSize();
            QPointF p = pos();
            p.setX(qRound(p.x() / gridSize) * gridSize);
            p.setY(qRound(p.y() / gridSize) * gridSize);
            setPos(p);
        }
        adjustConnections();
        modelNode_->setPos(QPoint(pos().x(), pos().y()));
        break;
    case ItemSelectedHasChanged:
        {
            adjust();
            if(value.toBool())
            {
                canvas()->raiseItem(this);
                oldText_ = text();
            }
            else
                exitEditMode(false);
        }
        break;
    default:
        break;
    };

    return QGraphicsItem::itemChange(change, value);
}

void QDataflowNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if(!isInEditMode())
    {
        enterEditMode();
        return;
    }
    event->ignore();
    QGraphicsItem::mouseDoubleClickEvent(event);
}

QDataflowIOlet::QDataflowIOlet(QDataflowNode *node, int index)
    : canvas_(node->canvas()), node_(node), index_(index)
{
    setAcceptHoverEvents(canvas_->showIOletTooltips());
}

void QDataflowIOlet::addConnection(QDataflowConnection *connection)
{
    connections_ << connection;
    connection->adjust();
}

void QDataflowIOlet::removeConnection(QDataflowConnection *connection)
{
    connections_.removeAll(connection);
}

QList<QDataflowConnection*> QDataflowIOlet::connections() const
{
    return connections_;
}

void QDataflowIOlet::adjustConnections() const
{
    for(auto *conn : connections_)
    {
        conn->adjust();
    }
}

void QDataflowIOlet::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);

    tooltip_->setVisible(true);
    QPointF p = tooltip_->mapToScene(QPointF(0,0));
    tooltip_->setParentItem(nullptr);
    tooltip_->setPos(p);
}

void QDataflowIOlet::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);

    tooltip_->setVisible(false);
    tooltip_->setParentItem(this);
    tooltip_->setPos(QPointF(0,0));
}

QRectF QDataflowIOlet::boundingRect() const
{
    QDataflowNode *n = node();
    QRectF r(-n->ioletWidth() / 2, -n->ioletHeight() / 2, n->ioletWidth(), n->ioletHeight());
    int tolerance = 5;
    r.adjust(-tolerance, -tolerance, tolerance, tolerance);
    return r;
}

void QDataflowIOlet::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    QDataflowNode *n = node();
    painter->fillRect(QRect(-n->ioletWidth() / 2, -n->ioletHeight() / 2, n->ioletWidth(), n->ioletHeight()), Qt::black);
}


QDataflowInlet::QDataflowInlet(QDataflowNode *node, int index)
    : QDataflowIOlet(node, index)
{
    tooltip_ = new QDataflowTooltip(this, node->modelNode()->inlet(index)->type(), QPointF(0, -20));
    tooltip_->setZValue(std::numeric_limits<qreal>::max());
    tooltip_->setVisible(false);
}

QDataflowOutlet::QDataflowOutlet(QDataflowNode *node, int index)
    : QDataflowIOlet(node, index), tmpConn_()

{
    tooltip_ = new QDataflowTooltip(this, node->modelNode()->outlet(index)->type(), QPointF(0, 20));
    tooltip_->setZValue(std::numeric_limits<qreal>::max());
    tooltip_->setVisible(false);

    setCursor(Qt::CrossCursor);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void QDataflowOutlet::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);

    tmpConn_ = new QGraphicsLineItem(this);
    tmpConn_->setPos(0, node()->ioletHeight() / 2);
    tmpConn_->setZValue(10000);
    tmpConn_->setPen(node()->tempConnectionPen());
    tmpConn_->setFlag(ItemStacksBehindParent);
    node()->canvas()->raiseItem(tmpConn_);
    node()->canvas()->raiseItem(node());
}

void QDataflowOutlet::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);

    setCursor(Qt::CrossCursor);

    if(tmpConn_)
    {
        node()->scene()->removeItem(tmpConn_);
        delete tmpConn_;
        tmpConn_ = 0;
    }

    if(QDataflowInlet *inlet = node()->canvas()->itemAtT<QDataflowInlet>(event->scenePos()))
    {
        QDataflowModel *model = node()->canvas()->model();
        model->connect(node()->modelNode(), index(), inlet->node()->modelNode(), inlet->index());
    }
}

void QDataflowOutlet::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if(tmpConn_)
    {
        tmpConn_->setLine(QLineF(QPointF(), tmpConn_->mapFromScene(event->scenePos())));

        // inlet under mouse:
        QDataflowInlet *inlet = node()->canvas()->itemAtT<QDataflowInlet>(event->scenePos());

        // give visual feedback about the connection being made:
        QDataflowModelOutlet *mdloutlet = node()->modelNode()->outlet(index());
        QDataflowModelInlet *mdlinlet = inlet ? inlet->node()->modelNode()->inlet(inlet->index()) : nullptr;
        if(inlet && mdloutlet->canMakeConnectionTo(mdlinlet) && mdlinlet->canAcceptConnectionFrom(mdloutlet))
        {
            tmpConn_->setPen(node()->connectionPen());
        }
        else if(inlet)
        {
            tmpConn_->setPen(node()->invalidConnectionPen());
        }
        else
        {
            tmpConn_->setPen(node()->tempConnectionPen());
        }
    }
}

QDataflowConnection::QDataflowConnection(QDataflowCanvas *canvas, QDataflowModelConnection *modelConnection)
    : canvas_(canvas), modelConnection_(modelConnection)
{
    setFlag(ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(canvas_->showConnectionHoverFeedback());

    QDataflowModelOutlet *src = modelConnection->source();
    QDataflowModelInlet *dst = modelConnection->dest();
    source_ = canvas->node(src->node())->outlet(src->index());
    dest_ = canvas->node(dst->node())->inlet(dst->index());
    source_->addConnection(this);
    dest_->addConnection(this);
    adjust();
}

QDataflowModelConnection * QDataflowConnection::modelConnection() const
{
    return modelConnection_;
}

void QDataflowConnection::adjust()
{
    if(!source_ || !dest_)
        return;

    prepareGeometryChange();

    sourcePoint_ = mapFromItem(source_, 0, source_->node()->ioletHeight() / 2);
    destPoint_ = mapFromItem(dest_, 0, -dest_->node()->ioletHeight() / 2);
}

QRectF QDataflowConnection::boundingRect() const
{
    if(!source_ || !dest_)
        return QRectF();

    qreal penWidth = 1;
    qreal extra = (penWidth) / 2.0;

    return QRectF(sourcePoint_, destPoint_).normalized()
        .adjusted(-extra, -extra, extra, extra);
}

QPainterPath QDataflowConnection::shape() const
{
    QPointF dp = destPoint_ - sourcePoint_;
    qreal angle = atan2(dp.y(), dp.x());
    QPointF a(cos(angle + M_PI_2), sin(angle + M_PI_2)),
            b(cos(angle - M_PI_2), sin(angle - M_PI_2));
    int k = source_->node()->ioletHeight();
    QPainterPath path;
    path.addPolygon(QPolygonF()
                    << (sourcePoint_ + k * a)
                    << (destPoint_ + k * a)
                    << (destPoint_ + k * b)
                    << (sourcePoint_ + k * b));
    return path;
}

void QDataflowConnection::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    if(!source_ || !dest_)
        return;

    QLineF line(sourcePoint_, destPoint_);
    if(qFuzzyCompare(line.length(), qreal(0.)))
        return;

    bool sel = option->state & QStyle::State_Selected,
            hov = option->state & QStyle::State_MouseOver;

    if(sel || hov)
    {
        painter->fillPath(shape(), sel ? Qt::cyan : Qt::gray);
    }

    painter->setPen(QPen(sel ? Qt::blue : Qt::black, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawLine(line);
}

QDataflowNodeTextLabel::QDataflowNodeTextLabel(QDataflowNode *node, QGraphicsItem *parent)
    : QGraphicsTextItem(parent), node_(node), completionIndex_(-1), completionActive_(false)
{
}

bool QDataflowNodeTextLabel::sceneEvent(QEvent *event)
{
    if(event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*> (event);
        switch(keyEvent->key())
        {
        case Qt::Key_Tab:
            return true;
        case Qt::Key_Escape:
            if(completionActive_) clearCompletion();
            else node_->exitEditMode(true);
            return true;
        case Qt::Key_Return:
            if(completionActive_) acceptCompletion();
            else node_->exitEditMode(false);
            return true;
        case Qt::Key_Down:
            if(completionActive_) cycleCompletion(1);
            return true;
        case Qt::Key_Up:
            if(completionActive_) cycleCompletion(-1);
            return true;
        default:
            break;
        }
    }
    return QGraphicsTextItem::sceneEvent(event);
}

void QDataflowNodeTextLabel::setCompletion(const QStringList &list)
{
    clearCompletion();

    if(list.empty()) return;

    completionActive_ = true;

    qreal y = boundingRect().height() + 1;
    for(auto &str : list)
    {
        QGraphicsRectItem *rectItem = new QGraphicsRectItem(this);
        rectItem->setPos(0, y);
        QGraphicsSimpleTextItem *item = new QGraphicsSimpleTextItem(rectItem);
        item->setText(str);
        completionRectItems_.push_back(rectItem);
        completionItems_.push_back(item);
        y += item->boundingRect().height();
    }
    qreal maxw = 0;
    for(auto *item : as_const(completionItems_))
        maxw = std::max(maxw, item->boundingRect().width());
    for(int i = 0; i < completionItems_.length(); i++)
    {
        QRectF r = completionItems_[i]->boundingRect();
        r.setWidth(maxw);
        completionRectItems_[i]->setRect(r);
    }

    node_->canvas()->raiseItem(this);

    updateCompletion();
}

void QDataflowNodeTextLabel::clearCompletion()
{
    for(auto *item : as_const(completionItems_))
    {
        node_->canvas()->scene()->removeItem(item);
        delete item;
    }
    completionItems_.clear();
    for(auto *item : as_const(completionRectItems_))
    {
        node_->canvas()->scene()->removeItem(item);
        delete item;
    }
    completionRectItems_.clear();
    completionIndex_ = -1;
    completionActive_ = false;
}

void QDataflowNodeTextLabel::acceptCompletion()
{
    if(completionActive_)
    {
        if(completionIndex_ >= 0)
        {
            document()->setPlainText(completionItems_[completionIndex_]->text());
        }
        else
        {
            node_->exitEditMode(false);
        }
    }
    else
    {
        clearCompletion();
    }
}

void QDataflowNodeTextLabel::cycleCompletion(int d)
{
    int n = completionItems_.length();
    if(completionIndex_ == -1 && d == -1) completionIndex_ = n - 1;
    else completionIndex_ += d;
    while(completionIndex_ < 0) completionIndex_ += n;
    while(completionIndex_ >= n) completionIndex_ -= n;
    updateCompletion();
}

void QDataflowNodeTextLabel::updateCompletion()
{
    for(int i = 0; i < completionItems_.length(); i++)
    {
        completionRectItems_[i]->setBrush(i == completionIndex_ ? Qt::blue : Qt::white);
        completionItems_[i]->setPen(QPen(i == completionIndex_ ? Qt::white : Qt::black));
    }
}

void QDataflowNodeTextLabel::complete()
{
    QString txt = document()->toPlainText();
    QStringList completionList = node_->canvas()->completion()->complete(txt);
    setCompletion(completionList);
}

void QDataflowNodeTextLabel::focusOutEvent(QFocusEvent *event)
{
    if(event->reason() == Qt::MouseFocusReason) return;
    QGraphicsTextItem::focusOutEvent(event);
}

QDataflowTooltip::QDataflowTooltip(QGraphicsItem *parentItem, const QString &text, const QPointF &offset)
    : QGraphicsItemGroup(parentItem), offset_(offset)
{
    shape_ = new QGraphicsPathItem(this);
    shape_->setPen(QPen(Qt::black, 1, Qt::SolidLine));
    shape_->setBrush(Qt::white);

    text_ = new QGraphicsSimpleTextItem(this);
    text_->setText(text);

    adjust();
}

void QDataflowTooltip::setText(const QString &text)
{
    text_->setText(text);

    adjust();
}

void QDataflowTooltip::adjust()
{
    text_->setPos(offset_ - text_->boundingRect().center());
    const int kb = 4; // margin
    const int kw = 6; // tip width
    QRectF br = text_->boundingRect().adjusted(-kb, -kb, kb, kb).translated(text_->pos());
    QPointF center(qBound(br.left() + kw, 0.0, br.right() - kw), br.center().y());
    double a = atan2(-center.y(), -center.x());
    QPainterPath pTip;
    pTip.lineTo(center + QPointF(cos(a + M_PI_4), sin(a + M_PI_4)) * kw);
    pTip.lineTo(center + QPointF(cos(a - M_PI_4), sin(a - M_PI_4)) * kw);
    pTip.closeSubpath();
    QPainterPath pRect;
    pRect.addRoundedRect(br, 1.5 * kb, 1.5 * kb);
    shape_->setPath(pTip.united(pRect).simplified());
}

QStringList QDataflowTextCompletion::complete(const QString &nodeText)
{
    Q_UNUSED(nodeText);
    return {};
}
