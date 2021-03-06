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
#ifndef QDATAFLOWCANVAS_H
#define QDATAFLOWCANVAS_H

#include <QGraphicsItem>
#include <QGraphicsView>

#include "qdataflowmodel.h"

class QDataflowNode;
class QDataflowInlet;
class QDataflowOutlet;
class QDataflowConnection;
class QDataflowTextCompletion;
class QDataflowNodeTextLabel;
class QDataflowTooltip;
class QGraphicsSceneMouseEvent;
class QMouseEvent;

enum QDataflowItemType {
    QDataflowItemTypeNode = QGraphicsItem::UserType + 1,
    QDataflowItemTypeConnection = QGraphicsItem::UserType + 2,
    QDataflowItemTypeInlet = QGraphicsItem::UserType + 3,
    QDataflowItemTypeOutlet = QGraphicsItem::UserType + 4
};

class QDataflowCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    QDataflowCanvas(QWidget *parent = {});
    ~QDataflowCanvas() override;

    QDataflowModel * model();
    void setModel(QDataflowModel *model);

    QDataflowNode * node(QDataflowModelNode *node);
    QDataflowConnection * connection(QDataflowModelConnection *conn);

    QList<QDataflowNode*> selectedNodes();
    QList<QDataflowConnection*> selectedConnections();

    bool isSomeNodeInEditMode() const;

    QDataflowTextCompletion * completion() const {return completion_;}
    void setCompletion(QDataflowTextCompletion *completion) {completion_ = completion;}

    void raiseItem(QGraphicsItem *item);

    bool showIOletTooltips();
    void setShowIOletTooltips(bool show);
    bool showObjectHoverFeedback();
    void setShowObjectHoverFeedback(bool show);
    bool showConnectionHoverFeedback();
    void setShowConnectionHoverFeedback(bool show);
    qreal gridSize();
    void setGridSize(qreal sz);
    bool drawGrid();
    void setDrawGrid(bool draw);

protected:
    template<typename T>
    T * itemAtT(const QPointF &point);

    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

protected Q_SLOTS:
    void itemTextEditorTextChange();
    void onNodeAdded(QDataflowModelNode *mdlnode);
    void onNodeRemoved(QDataflowModelNode *mdlnode);
    void onNodeValidChanged(QDataflowModelNode *mdlnode, bool valid);
    void onNodePosChanged(QDataflowModelNode *mdlnode, const QPoint &pos);
    void onNodeTextChanged(QDataflowModelNode *mdlnode, const QString &text);
    void onNodeInletCountChanged(QDataflowModelNode *mdlnode, int count);
    void onNodeOutletCountChanged(QDataflowModelNode *mdlnode, int count);
    void onConnectionAdded(QDataflowModelConnection *mdlconn);
    void onConnectionRemoved(QDataflowModelConnection *mdlconn);

    friend class QDataflowNode;
    friend class QDataflowIOlet;
    friend class QDataflowInlet;
    friend class QDataflowOutlet;
    friend class QDataflowConnection;

private:
    QDataflowModel *model_;
    QDataflowTextCompletion *completion_;
    QSet<QDataflowNode*> ownedNodes_;
    QSet<QDataflowConnection*> ownedConnections_;
    QMap<QDataflowModelNode*, QDataflowNode*> nodes_;
    QMap<QDataflowModelConnection*, QDataflowConnection*> connections_;
    bool showIOletsTooltips_;
    bool showObjectHoverFeedback_;
    bool showConnectionHoverFeedback_;
    qreal gridSize_;
    bool drawGrid_;
};

class QDataflowNode : public QGraphicsItem
{
protected:
    QDataflowNode(QDataflowCanvas *canvas_, QDataflowModelNode *modelNode);

public:
    QDataflowModelNode * modelNode() const;

    QDataflowInlet * inlet(int index) const {return inlets_.at(index);}
    int inletCount() const {return inlets_.size();}
    void setInletCount(int count, bool skipAdjust = false);
    QDataflowOutlet * outlet(int index) const {return outlets_.at(index);}
    int outletCount() const {return outlets_.size();}
    void setOutletCount(int count, bool skipAdjust = false);

    int type() const override {return QDataflowItemTypeNode;}

    void setText(const QString &text);
    QString text() const;

    void setValid(bool valid);
    bool isValid() const;

    void adjustConnections() const;

    QRectF boundingRect() const override;

    void adjust();

    QDataflowCanvas * canvas() const {return canvas_;}

    qreal ioletWidth() const {return 10;}
    qreal ioletHeight() const {return 4;}
    qreal ioletSpacing() const {return 13;}
    qreal inletsWidth() const;
    qreal outletsWidth() const;
    QPen objectPen() const;
    QBrush objectBrush() const;
    QBrush headerBrush() const;
    QPen tempConnectionPen() const;
    QPen connectionPen() const;
    QPen invalidConnectionPen() const;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void enterEditMode();
    void exitEditMode(bool revertText);
    bool isInEditMode() const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QDataflowCanvas *canvas_;
    QDataflowModelNode *modelNode_;
    QList<QDataflowInlet*> inlets_;
    QList<QDataflowOutlet*> outlets_;
    QGraphicsRectItem *inputHeader_;
    QGraphicsRectItem *objectBox_;
    QGraphicsRectItem *outputHeader_;
    QDataflowNodeTextLabel *textItem_;
    bool valid_;
    QString oldText_;

    friend class QDataflowCanvas;
};

class QDataflowIOlet : public QGraphicsItem
{
protected:
    QDataflowIOlet(QDataflowNode *node, int index);

public:
    QDataflowNode * node() const {return node_;}
    int index() const {return index_;}

    void addConnection(QDataflowConnection *connection);
    void removeConnection(QDataflowConnection *connection);
    QList<QDataflowConnection*> connections() const;
    void adjustConnections() const;

    QDataflowCanvas * canvas() const {return canvas_;}

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QDataflowCanvas *canvas_;
    QList<QDataflowConnection*> connections_;
    QDataflowNode *node_;
    int index_;

protected:
    QDataflowTooltip *tooltip_;

    friend class QDataflowCanvas;
    friend class QDataflowNode;
};

class QDataflowInlet : public QDataflowIOlet
{
protected:
    QDataflowInlet(QDataflowNode *node, int index);

public:
    int type() const override {return QDataflowItemTypeInlet;}

    void onDataRecevied(void *data);

    friend class QDataflowCanvas;
    friend class QDataflowNode;
};

class QDataflowOutlet : public QDataflowIOlet
{
protected:
    QDataflowOutlet(QDataflowNode *node, int index);

public:
    int type() const override {return QDataflowItemTypeOutlet;}

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QGraphicsLineItem *tmpConn_;

    friend class QDataflowCanvas;
    friend class QDataflowNode;
};

class QDataflowConnection : public QGraphicsItem
{
protected:
    QDataflowConnection(QDataflowCanvas *canvas, QDataflowModelConnection *modelConnection);

public:
    QDataflowModelConnection * modelConnection() const;

    QDataflowOutlet * source() const {return source_;}
    QDataflowInlet * dest() const {return dest_;}

    void adjust();

    QDataflowCanvas * canvas() const {return canvas_;}

    int type() const override {return QDataflowItemTypeConnection;}

protected:
    QRectF boundingRect() const override;
    QPainterPath shape() const override;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QDataflowCanvas *canvas_;
    QDataflowModelConnection *modelConnection_;
    QDataflowOutlet *source_;
    QDataflowInlet *dest_;
    QPointF sourcePoint_;
    QPointF destPoint_;

    friend class QDataflowCanvas;
    friend class QDataflowInlet;
    friend class QDataflowOutlet;
};

class QDataflowNodeTextLabel : public QGraphicsTextItem
{
protected:
    QDataflowNodeTextLabel(QDataflowNode *node, QGraphicsItem *parent);

public:
    void setCompletion(const QStringList &list);
    void clearCompletion();
    void acceptCompletion();
    void cycleCompletion(int d);
    void updateCompletion();
    void complete();

protected:
    bool sceneEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QDataflowNode *node_;
    QList<QGraphicsSimpleTextItem*> completionItems_;
    QList<QGraphicsRectItem*> completionRectItems_;
    int completionIndex_;
    bool completionActive_;

    friend class QDataflowNode;
};

class QDataflowTooltip : public QGraphicsItemGroup
{
protected:
    QDataflowTooltip(QGraphicsItem *parentItem, const QString &text, const QPointF &offset);

public:
    void setText(const QString &text);
    void adjust();

private:
    QGraphicsSimpleTextItem *text_;
    QGraphicsPathItem *shape_;
    QPointF offset_;

    friend class QDataflowCanvas;
    friend class QDataflowInlet;
    friend class QDataflowOutlet;
};

class QDataflowTextCompletion
{
public:
    virtual QStringList complete(const QString &nodeText);
    virtual ~QDataflowTextCompletion() = default;
};

template<typename T>
T * QDataflowCanvas::itemAtT(const QPointF &point)
{
    auto const items = scene()->items(point, Qt::IntersectsItemShape, Qt::DescendingOrder, transform());
    for(QGraphicsItem *item : items)
    {
         if(T *itemT = dynamic_cast<T*>(item))
             return itemT;
    }
    return {};
}


#endif // QDATAFLOWCANVAS_H
