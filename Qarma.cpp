/*
 *   Qarma - a Zenity clone for Qt4 and Qt5
 *   Copyright 2014 by Thomas Lübking <thomas.luebking@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "Qarma.h"

#include <QBoxLayout>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDate>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSocketNotifier>
#include <QStringBuilder>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QtDebug>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

#ifdef WS_X11
#include <QX11Info>
#include <X11/Xlib.h>
#endif

typedef QPair<QString, QString> Help;
typedef QList<Help> HelpList;
typedef QPair<QString, HelpList> CategoryHelp;
typedef QMap<QString, CategoryHelp> HelpDict;


Qarma::Qarma(int &argc, char **argv) : QApplication(argc, argv)
, m_helpMission(false)
, m_modal(false)
, m_parentWindow(0)
, m_timeout(0)
, m_notificationId(0)
, m_dialog(NULL)
, m_type(Invalid)
{
    if (argc < 2) {
        printHelp();
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        return;
    }

    QStringList argList = arguments(); // arguments() is slow
    QStringList args;
    for (int i = 1; i < argList.count(); ++i) {
        if (argList.at(i).startsWith("--")) {
            int split = argList.at(i).indexOf('=');
            if (split > -1) {
                args << argList.at(i).left(split) << argList.at(i).mid(split+1);
            } else {
                args << argList.at(i);
            }
        } else {
            args << argList.at(i);
        }
    }
    argList.clear();

    foreach (const QString &arg, args) {
        if (arg == "-h" || arg.startsWith("--help"))
            printHelp(arg.mid(7)); // "--help-"
    }

    if (m_helpMission) {
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        return;
    }

    if (!readGeneral(args))
        return;

    char error = 1;
    foreach (const QString &arg, args) {
        if (arg == "--calendar") {
            m_type = Calendar;
            error = showCalendar(args);
        } else if (arg == "--entry") {
            m_type = Entry;
            error = showEntry(args);
        } else if (arg == "--error") {
            m_type = Error;
            error = showMessage(args, 'e');
        } else if (arg == "--info") {
            m_type = Info;
            error = showMessage(args, 'i');
        } else if (arg == "--file-selection") {
            m_type = FileSelection;
            error = showFileSelection(args);
        } else if (arg == "--list") {
            m_type = List;
            error = showList(args);
        } else if (arg == "--notification") {
            m_type = Notification;
            error = showNotification(args);
        } else if (arg == "--progress") {
            m_type = Progress;
            error = showProgress(args);
        } else if (arg == "--question") {
            m_type = Question;
            error = showMessage(args, 'q');
        } else if (arg == "--warning") {
            m_type = Warning;
            error = showMessage(args, 'w');
        } else if (arg == "--scale") {
            m_type = Scale;
            error = showScale(args);
        } else if (arg == "--text-info") {
            m_type = TextInfo;
            error = showText(args);
        } else if (arg == "--color-selection") {
            m_type = ColorSelection;
            error = showColorSelection(args);
        } else if (arg == "--password") {
            m_type = Password;
            error = showPassword(args);
        } else if (arg == "--forms") {
            m_type = Forms;
            error = showForms(args);
        }
        if (error != 1) {
            break;
        }
    }
    if (error) {
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        return;
    }
    if (m_dialog) {
        if (!m_size.isNull()) {
            m_dialog->adjustSize();
            QSize sz = m_dialog->size();
            if (m_size.width() > 0)
                sz.setWidth(m_size.width());
            if (m_size.height() > 0)
                sz.setHeight(m_size.height());
            m_dialog->resize(sz);
        }
        m_dialog->setWindowModality(m_modal ? Qt::ApplicationModal : Qt::NonModal);
        if (!m_caption.isNull())
            m_dialog->setWindowTitle(m_caption);
        if (!m_icon.isNull())
            m_dialog->setWindowIcon(QIcon(m_icon));
        QDialogButtonBox *box = m_dialog->findChild<QDialogButtonBox*>();
        if (box && !m_ok.isNull()) {
            if (QPushButton *btn = box->button(QDialogButtonBox::Ok))
                btn->setText(m_ok);
        }
        if (box && !m_cancel.isNull()) {
            if (QPushButton *btn = box->button(QDialogButtonBox::Cancel))
                btn->setText(m_cancel);
        }
        if (m_parentWindow) {
#ifdef WS_X11
            m_dialog->setAttribute(Qt::WA_X11BypassTransientForHint);
            XSetTransientForHint(QX11Info::display(), m_dialog->winId(), m_parentWindow);
#endif
        }
    }
}

bool Qarma::error(const QString message)
{
    printf("Error: %s", qPrintable(message));
    QMetaObject::invokeMethod(this, "quitOnError", Qt::QueuedConnection);
    return true;
}

#define IF_IS(_TYPE_) if (const _TYPE_ *t = qobject_cast<const _TYPE_*>(w))

static QString value(const QWidget *w, const QString &pattern)
{
    if (!w)
        return QString();

    IF_IS(QLineEdit) {
        return t->text();
    } else IF_IS(QTreeWidget) {
        QString s;
        foreach (QTreeWidgetItem *item, t->selectedItems()) {
            for (int i = 0; i < t->columnCount(); ++i)
                s += item->text(i);
        }
        return s;
    } else IF_IS(QComboBox) {
        return t->currentText();
    } else IF_IS(QCalendarWidget) {
        if (pattern.isNull())
            return t->selectedDate().toString(Qt::SystemLocaleShortDate);
        return t->selectedDate().toString(pattern);
    } else IF_IS(QCheckBox) {
        return t->isChecked() ? "true" : "false";
    }
    return QString();
}

void Qarma::dialogFinished(int status)
{
    if (status != QDialog::Accepted) {
#ifdef Q_OS_UNIX
        if (sender()->property("qarma_autokill_parent").toBool()) {
            ::kill(getppid(), 15);
        }
#endif
        exit(1);
        return;
    }

    switch (m_type) {
        case Question:
        case Warning:
        case Info:
        case Error:
        case Progress:
        case Notification:
            break;
        case Calendar: {
            QString format = sender()->property("qarma_date_format").toString();
            QDate date = sender()->findChild<QCalendarWidget*>()->selectedDate();
            if (format.isEmpty())
                printf("%s\n", qPrintable(date.toString(Qt::SystemLocaleShortDate)));
            else
                printf("%s\n", qPrintable(date.toString(format)));
            break;
        }
        case Entry: {
            printf("%s\n", qPrintable(static_cast<QInputDialog*>(sender())->textValue()));
            break;
        }
        case Password: {
            QLineEdit   *username = sender()->findChild<QLineEdit*>("qarma_username"),
                        *password = sender()->findChild<QLineEdit*>("qarma_password");
            QString result;
            if (username)
                result = username->text() + '|';
            if (password)
                result += password->text();
            printf("%s\n", qPrintable(result));
            break;
        }
        case FileSelection: {
            QStringList files = static_cast<QFileDialog*>(sender())->selectedFiles();
            printf("%s\n", qPrintable(files.join(sender()->property("qarma_separator").toString())));
            break;
        }
        case ColorSelection: {
            printf("%s\n", qPrintable(static_cast<QColorDialog*>(sender())->selectedColor().name()));
            break;
        }
        case TextInfo: {
            QTextEdit *te = sender()->findChild<QTextEdit*>();
            if (te && !te->isReadOnly()) {
                printf("%s\n", qPrintable(te->toPlainText()));
                break;
            }
        }
        case Scale: {
            QSlider *sld = sender()->findChild<QSlider*>();
            if (sld) {
                printf("%s\n", qPrintable(QString::number(sld->value())));
                break;
            }
        }
        case List: {
            QTreeWidget *tw = sender()->findChild<QTreeWidget*>();
            QStringList result;
            if (tw) {
                bool done(false);
                foreach (const QTreeWidgetItem *twi, tw->selectedItems()) {
                    done = true;
                    result << twi->text(0);
                }
                if (!done) { // checkable
                    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                        const QTreeWidgetItem *twi = tw->topLevelItem(i);
                        if (twi->checkState(0) == Qt::Checked)
                            result << twi->text(1);
                    }
                }
            }
            printf("%s\n", qPrintable(result.join(sender()->property("qarma_separator").toString())));
            break;
        }
        case Forms: {
            QFormLayout *fl = sender()->findChild<QFormLayout*>();
            QStringList result;
            QString format = sender()->property("qarma_date_format").toString();
            for (int i = 0; i < fl->count(); ++i) {
                if (QLayoutItem *li = fl->itemAt(i, QFormLayout::FieldRole))
                    result << value(li->widget(), format);
            }
            printf("%s\n", qPrintable(result.join(sender()->property("qarma_separator").toString())));
            break;
        }
        default:
            qDebug() << "unhandled output" << m_type;
            break;
    }
    exit (0);
}

void Qarma::quitOnError()
{
    exit(1);
}

#define NEXT_ARG QString((++i < args.count()) ? args.at(i) : QString())
#define WARN_UNKNOWN_ARG(_KNOWN_) if (args.at(i).startsWith("--") && args.at(i) != _KNOWN_) qDebug() << "unspecific argument" << args.at(i);
#define SHOW_DIALOG m_dialog = dlg; connect(dlg, SIGNAL(finished(int)), SLOT(dialogFinished(int))); dlg->show();

bool Qarma::readGeneral(QStringList &args) {
    QStringList remains;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--title") {
            m_caption = NEXT_ARG;
        } else if (args.at(i) == "--window-icon") {
            m_icon = NEXT_ARG;
        } else if (args.at(i) == "--width") {
            bool ok;
            const int w = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--width must be followed by a positive number");
            m_size.setWidth(w);
        } else if (args.at(i) == "--height") {
            bool ok;
            const int h = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--height must be followed by a positive number");
            m_size.setHeight(h);
        } else if (args.at(i) == "--timeout") {
            bool ok;
            const int t = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--timeout must be followed by a positive number");
            QTimer::singleShot(t*1000, this, SLOT(quit()));
        } else if (args.at(i) == "--ok-label") {
            m_ok = NEXT_ARG;
        } else if (args.at(i) == "--ok-label") {
            m_ok = NEXT_ARG;
        } else if (args.at(i) == "--modal") {
            m_modal = true;
        } else if (args.at(i) == "--attach") {
            bool ok;
            const int w = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--attach must be followed by a positive number");
            m_parentWindow = w;
        } else {
            remains << args.at(i);
        }
    }
    args = remains;
    return true;
}

#define NEW_DIALOG QDialog *dlg = new QDialog; QVBoxLayout *vl = new QVBoxLayout(dlg);
#define FINISH_DIALOG(_BTNS_)   QDialogButtonBox *btns = new QDialogButtonBox(_BTNS_, Qt::Horizontal, dlg);\
                                vl->addWidget(btns);\
                                connect(btns, SIGNAL(accepted()), dlg, SLOT(accept()));\
                                connect(btns, SIGNAL(rejected()), dlg, SLOT(reject()));

char Qarma::showCalendar(const QStringList &args)
{
    NEW_DIALOG

    QDate date = QDate::currentDate();
    int d,m,y;
    date.getDate(&y, &m, &d);
    bool ok;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text") {
            vl->addWidget(new QLabel(NEXT_ARG, dlg));
        } else if (args.at(i) == "--day") {
            d = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--day must be followed by a positive number");
        } else if (args.at(i) == "--month") {
            m = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--month must be followed by a positive number");
        } else if (args.at(i) == "--year") {
            y = NEXT_ARG.toUInt(&ok);
            if (!ok)
                return !error("--year must be followed by a positive number");
        } else if (args.at(i) == "--date-format") {
            dlg->setProperty("qarma_date_format", NEXT_ARG);
        } else { WARN_UNKNOWN_ARG("--calendar") }
    }
    date.setDate(y, m, d);

    QCalendarWidget *cal = new QCalendarWidget(dlg);
    cal->setSelectedDate(date);
    vl->addWidget(cal);
    connect(cal, SIGNAL(activated(const QDate&)), dlg, SLOT(accept()));

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

char Qarma::showEntry(const QStringList &args)
{
    QInputDialog *dlg = new QInputDialog;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setLabelText(NEXT_ARG);
        else if (args.at(i) == "--entry-text")
            dlg->setTextValue(NEXT_ARG);
        else if (args.at(i) == "--hide-text")
            dlg->setTextEchoMode(QLineEdit::Password);
        else { WARN_UNKNOWN_ARG("--entry") }
    }
    SHOW_DIALOG

    return 0;
}

char Qarma::showPassword(const QStringList &args)
{
    NEW_DIALOG

    QLineEdit *username(NULL), *password(NULL);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--username") {
            vl->addWidget(new QLabel(tr("Enter username"), dlg));
            vl->addWidget(username = new QLineEdit(dlg));
            username->setObjectName("qarma_username");
            break;
        } else { WARN_UNKNOWN_ARG("--password") }
    }

    vl->addWidget(new QLabel(tr("Enter password"), dlg));
    vl->addWidget(password = new QLineEdit(dlg));
    password->setObjectName("qarma_password");
    password->setEchoMode(QLineEdit::Password);

    if (username)
        username->setFocus(Qt::OtherFocusReason);
    else
        password->setFocus(Qt::OtherFocusReason);

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

char Qarma::showMessage(const QStringList &args, char type)
{
    QMessageBox *dlg = new QMessageBox;
    dlg->setStandardButtons((type == 'q') ? QMessageBox::Ok|QMessageBox::Cancel : QMessageBox::Ok);
    dlg->setDefaultButton(QMessageBox::Ok);

    bool wrap = true, html = true;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setText(NEXT_ARG);
        else if (args.at(i) == "--icon-name")
            dlg->setIconPixmap(QIcon(NEXT_ARG).pixmap(64));
        else if (args.at(i) == "--no-wrap")
            wrap = false;
        else if (args.at(i) == "--no-markup")
            html = false;
        else if (args.at(i) == "--default-cancel")
            dlg->setDefaultButton(QMessageBox::Cancel);
        else if (args.at(i).startsWith("--") && args.at(i) != "--info" && args.at(i) != "--question" &&
                                                args.at(i) != "--warning" && args.at(i) != "--error")
            qDebug() << "unspecific argument" << args.at(i);
    }
    if (QLabel *l = dlg->findChild<QLabel*>("qt_msgbox_label")) {
        l->setWordWrap(wrap);
        l->setTextFormat(html ? Qt::RichText : Qt::PlainText);
    }
    if (dlg->iconPixmap().isNull())
        dlg->setIcon(type == 'w' ? QMessageBox::Warning :
                   (type == 'q' ? QMessageBox::Question :
                   (type == 'e' ? QMessageBox::Critical : QMessageBox::Information)));
    SHOW_DIALOG
    return 0;
}

char Qarma::showFileSelection(const QStringList &args)
{
    QFileDialog *dlg = new QFileDialog;
    dlg->setFileMode(QFileDialog::ExistingFile);
    dlg->setOption(QFileDialog::DontConfirmOverwrite, false);
    dlg->setProperty("qarma_separator", "|");
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--filename")
            dlg->selectFile(NEXT_ARG);
        else if (args.at(i) == "--multiple")
            dlg->setFileMode(QFileDialog::ExistingFiles);
        else if (args.at(i) == "--directory") {
            dlg->setFileMode(QFileDialog::Directory);
            dlg->setOption(QFileDialog::ShowDirsOnly);
        } else if (args.at(i) == "--save")
            dlg->setFileMode(QFileDialog::AnyFile);
        else if (args.at(i) == "--separator")
            dlg->setProperty("qarma_separator", NEXT_ARG);
        else if (args.at(i) == "--confirm-overwrite")
            dlg->setOption(QFileDialog::DontConfirmOverwrite);
        else if (args.at(i) == "--file-filter")
            dlg->setNameFilters(NEXT_ARG.split(" "));
        else { WARN_UNKNOWN_ARG("--file-selection") }
    }
    SHOW_DIALOG
    return 0;
}

void Qarma::toggleItems(QTreeWidgetItem *item, int column)
{
    if (column)
        return; // not the checkmark

    static bool recursion = false;
    if (recursion)
        return;

    recursion = true;
    QTreeWidget *tw = item->treeWidget();
    for (int i = 0; i < tw->topLevelItemCount(); ++i) {
        QTreeWidgetItem *twi = tw->topLevelItem(i);
        if (twi != item)
            twi->setCheckState(0, Qt::Unchecked);
    }
    recursion = false;
}

char Qarma::showList(const QStringList &args)
{
    NEW_DIALOG

    QLabel *lbl;
    vl->addWidget(lbl = new QLabel(dlg));

    QTreeWidget *tw;
    vl->addWidget(tw = new QTreeWidget(dlg));
    tw->setSelectionBehavior(QAbstractItemView::SelectRows);
    tw->setSelectionMode(QAbstractItemView::SingleSelection);
    tw->setRootIsDecorated(false);
    tw->setAllColumnsShowFocus(true);

    bool editable(false), checkable(false), exclusive(false), icons(false), ok;
    QStringList columns;
    QStringList values;
    QList<int> hiddenCols;
    dlg->setProperty("qarma_separator", "|");
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            lbl->setText(NEXT_ARG);
        else if (args.at(i) == "--multiple")
            tw->setSelectionMode(QAbstractItemView::ExtendedSelection);
        else if (args.at(i) == "--column") {
            columns << NEXT_ARG;
        } else if (args.at(i) == "--editable")
            editable = true;
        else if (args.at(i) == "--hide-header")
            tw->setHeaderHidden(true);
        else if (args.at(i) == "--separator")
            dlg->setProperty("qarma_separator", NEXT_ARG);
        else if (args.at(i) == "--hide-column") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                hiddenCols << v;
        } else if (args.at(i) == "--print-column") {
            qWarning("TODO: --print-column");
        } else if (args.at(i) == "--checklist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
            checkable = true;
        } else if (args.at(i) == "--radiolist") {
            tw->setSelectionMode(QAbstractItemView::NoSelection);
            tw->setAllColumnsShowFocus(false);
            checkable = true;
            exclusive = true;
        } else if (args.at(i) == "--imagelist") {
            icons = true;
        } else if (args.at(i) != "--list") {
            values << args.at(i);
        }
    }

    if (checkable)
        editable = false;

    int columnCount = qMax(columns.count(), 1);
    tw->setColumnCount(columnCount);
    tw->setHeaderLabels(columns);
    foreach (const int &i, hiddenCols)
        tw->setColumnHidden(i, true);

    for (int i = 0; i < values.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < columnCount; ++j) {
            itemValues << values.at(i++);
            if (i == values.count())
                break;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(tw, itemValues);
        Qt::ItemFlags flags = item->flags();
        if (editable)
            flags |= Qt::ItemIsEditable;
        if (checkable) {
            flags |= Qt::ItemIsUserCheckable;
            item->setCheckState(0, Qt::Unchecked);
        }
        if (icons)
            item->setIcon(0, QPixmap(item->text(0)));
        if (checkable || icons) {
            item->setData(0, Qt::EditRole, item->text(0));
            item->setText(0, QString());
        }
        item->setFlags(flags);
        tw->addTopLevelItem(item);
    }
    if (exclusive) {
        connect (tw, SIGNAL(itemChanged(QTreeWidgetItem*, int)), SLOT(toggleItems(QTreeWidgetItem*, int)));
    }
    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}

void Qarma::notify(const QString message, bool noClose)
{
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered("org.freedesktop.Notifications")) {
        QDBusInterface notifications("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications");
        const QString summary = (message.length() < 32) ? message : message.left(25) + "...";
        QVariantMap hintMap;
        QStringList hintList = m_notificationHints.split(':');
        for (int i = 0; i < hintList.count() - 1; i+=2)
            hintMap.insert(hintList.at(i), hintList.at(i+1));
        QDBusMessage msg = notifications.call("Notify", "Qarma", m_notificationId, "dialog-information", summary, message,
                                              QStringList() /*actions*/, hintMap, m_timeout);
        if (msg.arguments().count())
            m_notificationId = msg.arguments().at(0).toUInt();
        return;
    }

    QMessageBox *dlg = static_cast<QMessageBox*>(m_dialog);
    if (!dlg) {
        dlg = new QMessageBox;
        dlg->setIcon(QMessageBox::Information);
        dlg->setStandardButtons(noClose ? QMessageBox::NoButton : QMessageBox::Ok);
        dlg->setWindowFlags(Qt::ToolTip);
        dlg->setWindowOpacity(0.8);
        if (QLabel *l = dlg->findChild<QLabel*>("qt_msgbox_label")) {
            l->setWordWrap(true);
        }
    }
    dlg->setText(message);
    SHOW_DIALOG
    dlg->adjustSize();
    dlg->move(QApplication::desktop()->availableGeometry().topRight() - QPoint(dlg->width() + 20, -20));
}

char Qarma::showNotification(const QStringList &args)
{
    QString message;
    bool listening(false);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text") {
            message = NEXT_ARG;
        } else if (args.at(i) == "--listen") {
            listening = true;
            listenToStdIn();
        } else if (args.at(i) == "--hint") {
            m_notificationHints = NEXT_ARG;
        } else { WARN_UNKNOWN_ARG("--notification") }
    }
    if (!message.isEmpty())
        notify(message, listening);
    if (!(listening || m_dialog))
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
    return 0;
}

static QFile *gs_stdin = 0;

void Qarma::readStdIn()
{
    QSocketNotifier *notifier = qobject_cast<QSocketNotifier*>(sender());
    if (notifier)
        notifier->setEnabled(false);

    QByteArray ba = gs_stdin->readLine();
    if (ba.isEmpty() && notifier) {
        gs_stdin->close();
//         gs_stdin->deleteLater(); // hello segfault...
//         gs_stdin = NULL;
        notifier->deleteLater();
        return;
    }

    static QString cachedText;
    QString newText = QString::fromLocal8Bit(ba);
    if (newText.isEmpty() && cachedText.isEmpty()) {
        if (notifier)
            notifier->setEnabled(true);
        return;
    }

    QStringList input;
    if (m_type != TextInfo)
        input = newText.split('\n');
    if (m_type == Progress) {
        QProgressDialog *dlg = static_cast<QProgressDialog*>(m_dialog);
        const int oldValue = dlg->value();
        bool ok;
        foreach (QString line, input) {
            int u = line.toInt(&ok);
            if (ok)
                dlg->setValue(qMin(100,u));
        }
        if (dlg->value() == 100) {
            if (dlg->property("qarma_autoclose").toBool())
                QTimer::singleShot(250, this, SLOT(quit()));
            else {
                disconnect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
                connect (dlg, SIGNAL(canceled()), dlg, SLOT(accept()));
                dlg->setCancelButtonText(tr("Ok"));
            }
        } else if (oldValue == 100) {
            disconnect (dlg, SIGNAL(canceled()), dlg, SLOT(accept()));
            connect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
            dlg->setCancelButtonText(tr("Cancel"));
        }
    } else if (m_type == TextInfo) {
        if (QTextEdit *te = m_dialog->findChild<QTextEdit*>()) {
            cachedText += newText;
            static QPropertyAnimation *animator = NULL;
            if (!animator || animator->state() != QPropertyAnimation::Running) {
                const int oldValue = te->verticalScrollBar() ? te->verticalScrollBar()->value() : 0;
                te->setText(te->toPlainText() + cachedText);
                cachedText.clear();
                if (te->verticalScrollBar() && te->property("qarma_autoscroll").toBool()) {
                    te->verticalScrollBar()->setValue(oldValue);
                    if (!animator) {
                        animator = new QPropertyAnimation(te->verticalScrollBar(), "value", this);
                        animator->setEasingCurve(QEasingCurve::InOutCubic);
                        connect(animator, SIGNAL(finished()), SLOT(readStdIn()));
                    }
                    const int diff = te->verticalScrollBar()->maximum() - oldValue;
                    if (diff > 0) {
                        animator->setDuration(qMin(qMax(200, diff), 2500));
                        animator->setEndValue(te->verticalScrollBar()->maximum());
                        animator->start();
                    }
                }
            }
        }
    } else if (m_type == Notification) {
        bool userNeedsHelp = true;
        foreach (QString line, input) {
            int split = line.indexOf(':');
            if (split < 0)
                continue;
            if (line.left(split) == "icon") {
                userNeedsHelp = false;
                // TODO: some icon filename, seems gnome specific and i've no idea how to handle this atm.
                qWarning("'icon' command not yet supported - if you know what this is supposed to do, please file a bug");
            } else if (line.left(split) == "message" || line.left(split) == "tooltip") {
                userNeedsHelp = false;
                notify(line.mid(split+1));
            } else if (line.left(split) == "visible") {
                userNeedsHelp = false;
                if (m_dialog)
                    m_dialog->setVisible(line.mid(split+1).trimmed().compare("false", Qt::CaseInsensitive) &&
                                         line.mid(split+1).trimmed().compare("0", Qt::CaseInsensitive));
                else
                    qWarning("'visible' command only supported for failsafe dialog notification");
            } else if (line.left(split) == "hints") {
                m_notificationHints = line.mid(split+1);
            }
        }
        if (userNeedsHelp)
            qDebug() << "icon: <filename>\nmessage: <UTF-8 encoded text>\ntooltip: <UTF-8 encoded text>\nvisible: <true|false>";
    }
    if (notifier)
        notifier->setEnabled(true);
}

void Qarma::listenToStdIn()
{
    if (gs_stdin)
        return;
    gs_stdin = new QFile;
    if (gs_stdin->open(stdin, QIODevice::ReadOnly)) {
        QSocketNotifier *snr = new QSocketNotifier(gs_stdin->handle(), QSocketNotifier::Read, gs_stdin);
        connect (snr, SIGNAL(activated(int)), SLOT(readStdIn()));
    } else {
        delete gs_stdin;
        gs_stdin = NULL;
    }
}

char Qarma::showProgress(const QStringList &args)
{
    QProgressDialog *dlg = new QProgressDialog;
    dlg->setRange(0, 101);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            dlg->setLabelText(NEXT_ARG);
        else if (args.at(i) == "--percentage")
            dlg->setValue(NEXT_ARG.toUInt());
        else if (args.at(i) == "--pulsate")
            dlg->setRange(0,0);
        else if (args.at(i) == "--auto-close")
            dlg->setProperty("qarma_autoclose", true);
        else if (args.at(i) == "--auto-kill")
            dlg->setProperty("qarma_autokill_parent", true);
        else if (args.at(i) == "--no-cancel") {
            if (QPushButton *btn = dlg->findChild<QPushButton*>())
                btn->hide();
        } else { WARN_UNKNOWN_ARG("--progress") }
    }
    if (dlg->maximum() > 0) { // don't pulsate
        listenToStdIn();
    }
    connect (dlg, SIGNAL(canceled()), dlg, SLOT(reject()));
    SHOW_DIALOG
    return 0;
}

void Qarma::printInteger(int v)
{
    printf("%d\n", v);
}

char Qarma::showScale(const QStringList &args)
{
    NEW_DIALOG

    QHBoxLayout *hl = new QHBoxLayout;
    QLabel *lbl, *val;
    QSlider *sld;

    vl->addWidget(lbl = new QLabel("Enter a value", dlg));
    vl->addLayout(hl);
    hl->addWidget(sld = new QSlider(Qt::Horizontal, dlg));
    hl->addWidget(val = new QLabel(dlg));
    connect (sld, SIGNAL(valueChanged(int)), val, SLOT(setNum(int)));

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);

    sld->setRange(0,100);
    val->setNum(0);

    bool ok;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--text")
            lbl->setText(NEXT_ARG);
        else if (args.at(i) == "--value")
            sld->setValue(NEXT_ARG.toInt());
        else if (args.at(i) == "--min-value") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setMinimum(v);
        } else if (args.at(i) == "--max-value") {
            int v = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setMaximum(v);
        } else if (args.at(i) == "--step") {
            int u = NEXT_ARG.toInt(&ok);
            if (ok)
                sld->setSingleStep(u);
        } else if (args.at(i) == "--print-partial") {
            connect (sld, SIGNAL(valueChanged(int)), SLOT(printInteger(int)));
        } else if (args.at(i) == "--hide-value") {
            val->hide();
        } else { WARN_UNKNOWN_ARG("--scale") }
    }
    SHOW_DIALOG
    return 0;
}

char Qarma::showText(const QStringList &args)
{
    NEW_DIALOG

    QTextEdit *te;
    vl->addWidget(te = new QTextEdit(dlg));
    te->setReadOnly(true);
    QCheckBox *cb(NULL);

    bool needStdIn(true);
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--filename") {
            needStdIn = false;
            QFile file(NEXT_ARG);
            if (file.open(QIODevice::ReadOnly)) {
                te->setText(QString::fromLocal8Bit(file.readAll()));
                file.close();
            }
        } else if (args.at(i) == "--editable")
            te->setReadOnly(false);
        else if (args.at(i) == "--font") {
            te->setFont(QFont(NEXT_ARG));
        } else if (args.at(i) == "--checkbox") {
            vl->addWidget(cb = new QCheckBox(NEXT_ARG, dlg));
        } else if (args.at(i) == "--auto-scroll") {
            te->setProperty("qarma_autoscroll", true);
        } else { WARN_UNKNOWN_ARG("--text-info") }
    }

    if (te->isReadOnly()) {
        QPalette pal = te->viewport()->palette();
        for (int i = 0; i < 4; ++i) { // Disabled, Active, Inactive, Normal
            QPalette::ColorGroup cg = (QPalette::ColorGroup)i;
            pal.setColor(cg, QPalette::Base, pal.color(cg, QPalette::Window));
            pal.setColor(cg, QPalette::Text, pal.color(cg, QPalette::WindowText));
        }
        te->viewport()->setPalette(pal);
        te->viewport()->setAutoFillBackground(false);
        te->setFrameStyle(QFrame::NoFrame);
    }

    if (needStdIn)
        listenToStdIn();

    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);

    if (cb) {
        QPushButton *btn = btns->button(QDialogButtonBox::Ok);
        btn->setEnabled(false);
        connect(cb, SIGNAL(toggled(bool)), btn, SLOT(setEnabled(bool)));
    }

    SHOW_DIALOG
    return 0;
}

char Qarma::showColorSelection(const QStringList &args)
{
    QColorDialog *dlg = new QColorDialog;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--color") {
            dlg->setCurrentColor(QColor(NEXT_ARG));
        } else if (args.at(i) == "--show-palette") {
            qWarning("The show-palette parameter is not supported by qarma. Sorry.");
            void(0);
        } else { WARN_UNKNOWN_ARG("--color-selection") }
    }
    SHOW_DIALOG
    return 0;
}

static void buildList(QTreeWidget **tree, QStringList &values, QStringList &columns, bool &showHeader)
{
    QTreeWidget *tw = *tree;

    if (!tw)
        return;

    int columnCount = columns.count();
    tw->setHeaderHidden(!showHeader);
    if (columns.count()) {
        tw->setColumnCount(columns.count());
        tw->setHeaderLabels(columns);
    } else {
        columnCount = 1;
    }


    for (int i = 0; i < values.count(); ) {
        QStringList itemValues;
        for (int j = 0; j < columnCount; ++j) {
            itemValues << values.at(i++);
            if (i == values.count())
                break;
        }
        tw->addTopLevelItem(new QTreeWidgetItem(tw, itemValues));
    }

    for (int i = 0; i < columns.count(); ++i)
        tw->resizeColumnToContents(i);

    values.clear();
    columns.clear();
    showHeader = false;
    *tree = NULL;
}

char Qarma::showForms(const QStringList &args)
{
    NEW_DIALOG
    dlg->setProperty("qarma_separator", "|");

    QLabel *label;
    vl->addWidget(label = new QLabel(dlg));
    QFont fnt = label->font();
    fnt.setBold(true);
    label->setFont(fnt);

    QFormLayout *fl;
    vl->addLayout(fl = new QFormLayout);

    QTreeWidget *lastList = NULL;
    QStringList lastListValues, lastListColumns, lastComboValues;
    bool lastListHeader(false);
    QComboBox *lastCombo = NULL;
    for (int i = 0; i < args.count(); ++i) {
        if (args.at(i) == "--add-entry") {
            fl->addRow(NEXT_ARG, new QLineEdit(dlg));
        } else if (args.at(i) == "--add-password") {
            QLineEdit *le;
            fl->addRow(NEXT_ARG, le = new QLineEdit(dlg));
            le->setEchoMode(QLineEdit::Password);
        } else if (args.at(i) == "--add-calendar") {
            fl->addRow(NEXT_ARG, new QCalendarWidget(dlg));
        } else if (args.at(i) == "--add-list") {
            buildList(&lastList, lastListValues, lastListColumns, lastListHeader);
            fl->addRow(NEXT_ARG, lastList = new QTreeWidget(dlg));
        } else if (args.at(i) == "--list-values") {
            lastListValues = NEXT_ARG.split('|');
        } else if (args.at(i) == "--column-values") {
            lastListColumns = NEXT_ARG.split('|');
        } else if (args.at(i) == "--add-combo") {
            fl->addRow(NEXT_ARG, lastCombo = new QComboBox(dlg));
            lastCombo->addItems(lastComboValues);
            lastComboValues.clear();
        } else if (args.at(i) == "--combo-values") {
            lastComboValues = NEXT_ARG.split('|');
            if (lastCombo) {
                lastCombo->addItems(lastComboValues);
                lastComboValues.clear();
                lastCombo = NULL;
            }
        } else if (args.at(i) == "--show-header") {
            lastListHeader = true;
        } else if (args.at(i) == "--text") {
            label->setText(NEXT_ARG);
        } else if (args.at(i) == "--separator") {
            dlg->setProperty("qarma_separator", NEXT_ARG);
        } else if (args.at(i) == "--forms-date-format") {
            dlg->setProperty("qarma_date_format", NEXT_ARG);
        } else if (args.at(i) == "--add-checkbox") {
            fl->addRow(new QCheckBox(NEXT_ARG, dlg));
        } else { WARN_UNKNOWN_ARG("--forms") }
    }
    buildList(&lastList, lastListValues, lastListColumns, lastListHeader);


    FINISH_DIALOG(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    SHOW_DIALOG
    return 0;
}


void Qarma::printHelp(const QString &category)
{
    m_helpMission = true;

    static HelpDict helpDict;
    if (helpDict.isEmpty()) {
        helpDict["help"] = CategoryHelp(tr("Help options"), HelpList() <<
                            Help("-h, --help", tr("Show help options")) <<
                            Help("--help-all", tr("Show all help options")) <<
                            Help("--help-general", tr("Show general options")) <<
                            Help("--help-calendar", tr("Show calendar options")) <<
                            Help("--help-entry", tr("Show text entry options")) <<
                            Help("--help-error", tr("Show error options")) <<
                            Help("--help-info", tr("Show info options")) <<
                            Help("--help-file-selection", tr("Show file selection options")) <<
                            Help("--help-list", tr("Show list options")) <<
                            Help("--help-notification", tr("Show notification icon options")) <<
                            Help("--help-progress", tr("Show progress options")) <<
                            Help("--help-question", tr("Show question options")) <<
                            Help("--help-warning", tr("Show warning options")) <<
                            Help("--help-scale", tr("Show scale options")) <<
                            Help("--help-text-info", tr("Show text information options")) <<
                            Help("--help-color-selection", tr("Show color selection options")) <<
                            Help("--help-password", tr("Show password dialog options")) <<
                            Help("--help-forms", tr("Show forms dialog options")) <<
                            Help("--help-misc", tr("Show miscellaneous options")) <<
                            Help("--help-qt", tr("Show Qt Options")));
        helpDict["general"] = CategoryHelp(tr("General options"), HelpList() <<
                            Help("--title=TITLE", tr("Set the dialog title")) <<
                            Help("--window-icon=ICONPATH", tr("Set the window icon")) <<
                            Help("--width=WIDTH", tr("Set the width")) <<
                            Help("--height=HEIGHT", tr("Set the height")) <<
                            Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")) <<
                            Help("--ok-label=TEXT", tr("Sets the label of the Ok button")) <<
                            Help("--cancel-label=TEXT", tr("Sets the label of the Cancel button")) <<
                            Help("--modal", tr("Set the modal hint")) <<
                            Help("--attach=WINDOW", tr("Set the parent window to attach to")));
        helpDict["calendar"] = CategoryHelp(tr("Calendar options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--day=DAY", tr("Set the calendar day")) <<
                            Help("--month=MONTH", tr("Set the calendar month")) <<
                            Help("--year=YEAR", tr("Set the calendar year")) <<
                            Help("--timeout=TIMEOUT", tr("Set dialog timeout in seconds")) <<
                            Help("--date-format=PATTERN", tr("Set the format for the returned date")));
        helpDict["entry"] = CategoryHelp(tr("Text entry options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--entry-text=TEXT", tr("Set the entry text")) <<
                            Help("--hide-text", tr("Hide the entry text")));
        helpDict["error"] = CategoryHelp(tr("Error options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("--no-markup", tr("Do not enable html markup")));
        helpDict["info"] = CategoryHelp(tr("Info options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("--no-markup", tr("Do not enable html markup")));
        helpDict["file-selection"] = CategoryHelp(tr("File selection options"), HelpList() <<
                            Help("--filename=FILENAME", tr("Set the filename")) <<
                            Help("--multiple", tr("Allow multiple files to be selected")) <<
                            Help("--directory", tr("Activate directory-only selection")) <<
                            Help("--save", tr("Activate save mode")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")) <<
                            Help("--confirm-overwrite", tr("Confirm file selection if filename already exists")) <<
                            Help("--file-filter=NAME | PATTERN1 PATTERN2 ...", tr("Sets a filename filter")));
        helpDict["list"] = CategoryHelp(tr("List options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--column=COLUMN", tr("Set the column header")) <<
                            Help("--checklist", tr("Use check boxes for first column")) <<
                            Help("--radiolist", tr("Use radio buttons for first column")) <<
                            Help("--imagelist", tr("Use an image for first column")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")) <<
                            Help("--multiple", tr("Allow multiple rows to be selected")) <<
                            Help("--editable", tr("Allow changes to text")) <<
                            Help("--print-column=NUMBER", tr("Print a specific column (Default is 1. 'ALL' can be used to print all columns)")) <<
                            Help("--hide-column=NUMBER", tr("Hide a specific column")) <<
                            Help("--hide-header", tr("Hides the column headers")));
        helpDict["notification"] = CategoryHelp(tr("Notification icon options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--listen", tr("Listen for commands on stdin")) <<
                            Help("--hint=TEXT", tr("Set the notification hints")));
        helpDict["progress"] = CategoryHelp(tr("Progress options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--percentage=PERCENTAGE", tr("Set initial percentage")) <<
                            Help("--pulsate", tr("Pulsate progress bar")) <<
                            Help("--auto-close", tr("Dismiss the dialog when 100% has been reached")) <<
                            Help("--auto-kill", tr("Kill parent process if Cancel button is pressed")) <<
                            Help("--no-cancel", tr("Hide Cancel button")));
        helpDict["question"] = CategoryHelp(tr("Question options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("--no-markup", tr("Do not enable html markup")) <<
                            Help("--default-cancel", tr("Give cancel button focus by default")));
        helpDict["warning"] = CategoryHelp(tr("Warning options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--icon-name=ICON-NAME", tr("Set the dialog icon")) <<
                            Help("--no-wrap", tr("Do not enable text wrapping")) <<
                            Help("--no-markup", tr("Do not enable html markup")));
        helpDict["scale"] = CategoryHelp(tr("Scale options"), HelpList() <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--value=VALUE", tr("Set initial value")) <<
                            Help("--min-value=VALUE", tr("Set minimum value")) <<
                            Help("--max-value=VALUE", tr("Set maximum value")) <<
                            Help("--step=VALUE", tr("Set step size")) <<
                            Help("--print-partial", tr("Print partial values")) <<
                            Help("--hide-value", tr("Hide value")));
        helpDict["text-info"] = CategoryHelp(tr("Text information options"), HelpList() <<
                            Help("--filename=FILENAME", tr("Open file")) <<
                            Help("--editable", tr("Allow changes to text")) <<
                            Help("--font=TEXT", tr("Set the text font")) <<
                            Help("--checkbox=TEXT", tr("Enable an I read and agree checkbox")) <<
                            Help("--auto-scroll", tr("Auto scroll the text to the end. Only when text is captured from stdin")));
        helpDict["color-selection"] = CategoryHelp(tr("Color selection options"), HelpList() <<
                            Help("--color=VALUE", tr("Set the color")) <<
                            Help("--show-palette", tr("Show the palette")));
        helpDict["password"] = CategoryHelp(tr("Password dialog options"), HelpList() <<
                            Help("--username", tr("Display the username option")));
        helpDict["forms"] = CategoryHelp(tr("Forms dialog options"), HelpList() <<
                            Help("--add-entry=Field name", tr("Add a new Entry in forms dialog")) <<
                            Help("--add-password=Field name", tr("Add a new Password Entry in forms dialog")) <<
                            Help("--add-calendar=Calendar field name", tr("Add a new Calendar in forms dialog")) <<
                            Help("--add-list=List field and header name", tr("Add a new List in forms dialog")) <<
                            Help("--list-values=List of values separated by |", tr("List of values for List")) <<
                            Help("--column-values=List of values separated by |", tr("List of values for columns")) <<
                            Help("--add-combo=Combo box field name", tr("Add a new combo box in forms dialog")) <<
                            Help("--combo-values=List of values separated by |", tr("List of values for combo box")) <<
                            Help("--show-header", tr("Show the columns header")) <<
                            Help("--text=TEXT", tr("Set the dialog text")) <<
                            Help("--separator=SEPARATOR", tr("Set output separator character")) <<
                            Help("--forms-date-format=PATTERN", tr("Set the format for the returned date")) <<
                            Help("--add-checkbox=Checkbox label", "QARMA ONLY! " + tr("Add a new Checkbox forms dialog")));
        helpDict["misc"] = CategoryHelp(tr("Miscellaneous options"), HelpList() <<
                            Help("--about", tr("About Qarma")) <<
                            Help("--version", tr("Print version")));
        helpDict["qt"] = CategoryHelp(tr("Qt options"), HelpList() <<
                            Help("--foo", tr("Foo")) <<
                            Help("--bar", tr("Bar")));
        helpDict["application"] = CategoryHelp(tr("Application Options"), HelpList() <<
                            Help("--calendar", tr("Display calendar dialog")) <<
                            Help("--entry", tr("Display text entry dialog")) <<
                            Help("--error", tr("Display error dialog")) <<
                            Help("--info", tr("Display info dialog")) <<
                            Help("--file-selection", tr("Display file selection dialog")) <<
                            Help("--list", tr("Display list dialog")) <<
                            Help("--notification", tr("Display notification")) <<
                            Help("--progress", tr("Display progress indication dialog")) <<
                            Help("--question", tr("Display question dialog")) <<
                            Help("--warning", tr("Display warning dialog")) <<
                            Help("--scale", tr("Display scale dialog")) <<
                            Help("--text-info", tr("Display text information dialog")) <<
                            Help("--color-selection", tr("Display color selection dialog")) <<
                            Help("--password", tr("Display password dialog")) <<
                            Help("--forms", tr("Display forms dialog")) <<
                            Help("--display=DISPLAY", tr("X display to use")));
    }

    HelpDict::const_iterator it = helpDict.constEnd();
    if (!category.isEmpty())
        it = helpDict.find(category);

    if (it == helpDict.constEnd()) {
        printf("Usage:\n  %s [OPTION ...]\n\n", qPrintable(applicationName()));
        printHelp("help");
        printHelp("application");
        return;
    }

    printf("%s\n", qPrintable(it->first));
    foreach (const Help &help, it->second)
        printf("  %-53s%s\n", qPrintable(help.first), qPrintable(help.second));
    printf("\n");
}

int main (int argc, char **argv)
{
    Qarma d(argc, argv);
    return d.exec();
}