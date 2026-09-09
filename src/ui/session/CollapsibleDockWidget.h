// CollapsibleDockWidget.h
#ifndef COLLAPSIBLEDOCKWIDGET_H
#define COLLAPSIBLEDOCKWIDGET_H

#include <QDockWidget>
#include <QPushButton>
#include <QWidget>

class CollapsibleDockWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit CollapsibleDockWidget(QWidget *parent = nullptr);
    void setContentWidget(QWidget *widget);

private slots:
    void toggleCollapse();

protected:
    void changeEvent(QEvent *event) override;

private:
    QPushButton *toggleButton_;
    QWidget *contentWidget_;
    QWidget *titleBar_;
    int expandedWidth_;
    bool collapsed_;

    static constexpr int COLLAPSED_WIDTH = 24;
    static constexpr int MIN_WIDTH = 20;
    static constexpr int MAX_WIDTH = 480;
};


#endif
