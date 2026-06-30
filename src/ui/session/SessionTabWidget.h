#ifndef QSHELL_SESSIONTABWIDGET_H
#define QSHELL_SESSIONTABWIDGET_H

#include <QTabWidget>
#include <QTabBar>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionTab>

#ifdef Q_OS_MACOS
class LeftAlignedTabStyle : public QProxyStyle {
public:
    LeftAlignedTabStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    int styleHint(StyleHint hint, const QStyleOption *option,
                  const QWidget *widget, QStyleHintReturn *returnData) const override {
        if (hint == QStyle::SH_TabBar_Alignment)
            return Qt::AlignLeft;
        if (hint == QStyle::SH_TabBar_PreferNoArrows)
            return 0;
        if (hint == QStyle::SH_TabBar_ElideMode)
            return Qt::ElideNone;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget) const override {
        if (element == SE_TabWidgetTabBar) {
            QRect rect = QProxyStyle::subElementRect(element, option, widget);
            rect.moveLeft(0);
            return rect;
        }
        return QProxyStyle::subElementRect(element, option, widget);
    }
};
#endif

class SessionTabWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit SessionTabWidget(QWidget *parent = nullptr);
    ~SessionTabWidget() override = default;

protected:
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

private slots:
    void showTabContextMenu(const QPoint &pos);
    void renameTab();

private:
    int m_contextMenuTabIndex = -1;
};

#endif // QSHELL_SESSIONTABWIDGET_H
