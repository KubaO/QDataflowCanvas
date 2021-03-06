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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "qdataflowcanvas.h"

class MainWindow : public QMainWindow, private Ui::MainWindow, private QDataflowTextCompletion
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = {});

    QStringList complete(const QString &txt) override;

private:
    QDataflowModelNode *sourceNode;
    QStringList classList;

private Q_SLOTS:
    void setupNode(QDataflowModelNode *node);
    void processData();
    void onNodeAdded(QDataflowModelNode *node);
    void onNodeTextChanged(QDataflowModelNode *node, const QString &text);
    void onSelectionChanged();
    void onDumpModel();
};

#endif // MAINWINDOW_H
