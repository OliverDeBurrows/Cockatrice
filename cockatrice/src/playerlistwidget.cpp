#include <QHeaderView>
#include "playerlistwidget.h"
#include "pixmapgenerator.h"
#include "abstractclient.h"
#include "tab_game.h"
#include "tab_supervisor.h"
#include "tab_userlists.h"
#include "userlist.h"
#include "userinfobox.h"
#include <QMouseEvent>
#include <QAction>
#include <QMenu>

#include "pb/session_commands.pb.h"
#include "pb/command_kick_from_game.pb.h"
#include "pb/serverinfo_playerproperties.pb.h"

PlayerListItemDelegate::PlayerListItemDelegate(QObject *const parent)
	: QStyledItemDelegate(parent)
{
}

bool PlayerListItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
{
	if ((event->type() == QEvent::MouseButtonPress) && index.isValid()) {
		QMouseEvent *const mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::RightButton) {
			static_cast<PlayerListWidget *>(parent())->showContextMenu(mouseEvent->globalPos(), index);
			return true;
		}
	}
	return QStyledItemDelegate::editorEvent(event, model, option, index);
}

PlayerListTWI::PlayerListTWI()
	: QTreeWidgetItem(Type)
{
}

bool PlayerListTWI::operator<(const QTreeWidgetItem &other) const
{
	// Sort by spectator/player
	if (data(1, Qt::UserRole) != other.data(1, Qt::UserRole))
		return data(1, Qt::UserRole).toBool();
	
	// Sort by player ID
	return data(4, Qt::UserRole + 1).toInt() < other.data(4, Qt::UserRole + 1).toInt();
}

PlayerListWidget::PlayerListWidget(TabSupervisor *_tabSupervisor, AbstractClient *_client, TabGame *_game, QWidget *parent)
	: QTreeWidget(parent), tabSupervisor(_tabSupervisor), client(_client), game(_game), gameStarted(false)
{
	readyIcon = QIcon(":/resources/icon_ready_start.svg");
	notReadyIcon = QIcon(":/resources/icon_not_ready_start.svg");
	concededIcon = QIcon(":/resources/icon_conceded.svg");
	playerIcon = QIcon(":/resources/icon_player.svg");
	spectatorIcon = QIcon(":/resources/icon_spectator.svg");
	lockIcon = QIcon(":/resources/lock.svg");
	
	if (tabSupervisor) {
		itemDelegate = new PlayerListItemDelegate(this);
		setItemDelegate(itemDelegate);
	}
	
	setMinimumHeight(60);
	setIconSize(QSize(20, 15));
	setColumnCount(6);
	setHeaderHidden(true);
	setRootIsDecorated(false);
	header()->setResizeMode(QHeaderView::ResizeToContents);
	retranslateUi();
}

void PlayerListWidget::retranslateUi()
{
}

void PlayerListWidget::addPlayer(const ServerInfo_PlayerProperties &player)
{
	QTreeWidgetItem *newPlayer = new PlayerListTWI;
	players.insert(player.player_id(), newPlayer);
	updatePlayerProperties(player);
	addTopLevelItem(newPlayer);
	sortItems(1, Qt::AscendingOrder);
}

void PlayerListWidget::updatePlayerProperties(const ServerInfo_PlayerProperties &prop, int playerId)
{
	if (playerId == -1)
		playerId = prop.player_id();
	
	QTreeWidgetItem *player = players.value(playerId, 0);
	if (!player)
		return;
	
	if (prop.has_spectator()) {
		player->setIcon(1, prop.spectator() ? spectatorIcon : playerIcon);
		player->setData(1, Qt::UserRole, !prop.spectator());
	}
	if (prop.has_conceded())
		player->setData(2, Qt::UserRole, prop.conceded());
	if (prop.has_ready_start())
		player->setData(2, Qt::UserRole + 1, prop.ready_start());
	if (prop.has_conceded() || prop.has_ready_start())
		player->setIcon(2, gameStarted ? (prop.conceded() ? concededIcon : QIcon()) : (prop.ready_start() ? readyIcon : notReadyIcon));
	if (prop.has_user_info()) {
		player->setData(3, Qt::UserRole, prop.user_info().user_level());
		player->setIcon(3, QIcon(UserLevelPixmapGenerator::generatePixmap(12, prop.user_info().user_level())));
		player->setText(4, QString::fromStdString(prop.user_info().name()));
		const QString country = QString::fromStdString(prop.user_info().country());
		if (!country.isEmpty())
			player->setIcon(4, QIcon(CountryPixmapGenerator::generatePixmap(12, country)));
		player->setData(4, Qt::UserRole, QString::fromStdString(prop.user_info().name()));
	}
	if (prop.has_player_id())
		player->setData(4, Qt::UserRole + 1, prop.player_id());
	if (prop.has_deck_hash())
		player->setText(5, QString::fromStdString(prop.deck_hash()));
	if (prop.has_sideboard_locked())
		player->setIcon(5, prop.sideboard_locked() ? lockIcon : QIcon());
	if (prop.has_ping_seconds())
		player->setIcon(0, QIcon(PingPixmapGenerator::generatePixmap(12, prop.ping_seconds(), 10)));
}

void PlayerListWidget::removePlayer(int playerId)
{
	QTreeWidgetItem *player = players.value(playerId, 0);
	if (!player)
		return;
	players.remove(playerId);
	delete takeTopLevelItem(indexOfTopLevelItem(player));
}

void PlayerListWidget::setActivePlayer(int playerId)
{
	QMapIterator<int, QTreeWidgetItem *> i(players);
	while (i.hasNext()) {
		i.next();
		QTreeWidgetItem *twi = i.value();
		QColor c = i.key() == playerId ? QColor(150, 255, 150) : Qt::white;
		twi->setBackground(4, c);
	}
}

void PlayerListWidget::setGameStarted(bool _gameStarted, bool resuming)
{
	gameStarted = _gameStarted;
	QMapIterator<int, QTreeWidgetItem *> i(players);
	while (i.hasNext()) {
		QTreeWidgetItem *twi = i.next().value();
		if (gameStarted) {
			if (resuming)
				twi->setIcon(2, twi->data(2, Qt::UserRole).toBool() ? concededIcon : QIcon());
			else {
				twi->setData(2, Qt::UserRole, false);
				twi->setIcon(2, QIcon());
			}
		} else
			twi->setIcon(2, notReadyIcon);
	}
}

void PlayerListWidget::showContextMenu(const QPoint &pos, const QModelIndex &index)
{
	const QString &userName = index.sibling(index.row(), 4).data(Qt::UserRole).toString();
	int playerId = index.sibling(index.row(), 4).data(Qt::UserRole + 1).toInt();
	ServerInfo_User::UserLevelFlags userLevel = static_cast<ServerInfo_User::UserLevelFlags>(index.sibling(index.row(), 3).data(Qt::UserRole).toInt());
	
	QAction *aUserName = new QAction(userName, this);
	aUserName->setEnabled(false);
	QAction *aDetails = new QAction(tr("User &details"), this);
	QAction *aChat = new QAction(tr("Direct &chat"), this);
	QAction *aAddToBuddyList = new QAction(tr("Add to &buddy list"), this);
	QAction *aRemoveFromBuddyList = new QAction(tr("Remove from &buddy list"), this);
	QAction *aAddToIgnoreList = new QAction(tr("Add to &ignore list"), this);
	QAction *aRemoveFromIgnoreList = new QAction(tr("Remove from &ignore list"), this);
	QAction *aKick = new QAction(tr("Kick from &game"), this);
	
	QMenu *menu = new QMenu(this);
	menu->addAction(aUserName);
	menu->addSeparator();
	menu->addAction(aDetails);
	menu->addAction(aChat);
	if ((userLevel & ServerInfo_User::IsRegistered) && (tabSupervisor->getUserLevel() & ServerInfo_User::IsRegistered)) {
		menu->addSeparator();
		if (tabSupervisor->getUserListsTab()->getBuddyList()->getUsers().contains(userName))
			menu->addAction(aRemoveFromBuddyList);
		else
			menu->addAction(aAddToBuddyList);
		if (tabSupervisor->getUserListsTab()->getIgnoreList()->getUsers().contains(userName))
			menu->addAction(aRemoveFromIgnoreList);
		else
			menu->addAction(aAddToIgnoreList);
	}
	if (game->isHost() || !game->getTabSupervisor()->getAdminLocked()) {
		menu->addSeparator();
		menu->addAction(aKick);
	}
	if (userName == QString::fromStdString(game->getTabSupervisor()->getUserInfo()->name())) {
		aChat->setEnabled(false);
		aAddToBuddyList->setEnabled(false);
		aRemoveFromBuddyList->setEnabled(false);
		aAddToIgnoreList->setEnabled(false);
		aRemoveFromIgnoreList->setEnabled(false);
		aKick->setEnabled(false);
	}
	
	QAction *actionClicked = menu->exec(pos);
	if (actionClicked == aDetails) {
		UserInfoBox *infoWidget = new UserInfoBox(client, true, this, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint);
		infoWidget->setAttribute(Qt::WA_DeleteOnClose);
		infoWidget->updateInfo(userName);
	} else if (actionClicked == aChat)
		emit openMessageDialog(userName, true);
	else if (actionClicked == aAddToBuddyList) {
		Command_AddToList cmd;
		cmd.set_list("buddy");
		cmd.set_user_name(userName.toStdString());
		
		client->sendCommand(client->prepareSessionCommand(cmd));
	} else if (actionClicked == aRemoveFromBuddyList) {
		Command_RemoveFromList cmd;
		cmd.set_list("buddy");
		cmd.set_user_name(userName.toStdString());
		
		client->sendCommand(client->prepareSessionCommand(cmd));
	} else if (actionClicked == aAddToIgnoreList) {
		Command_AddToList cmd;
		cmd.set_list("ignore");
		cmd.set_user_name(userName.toStdString());
		
		client->sendCommand(client->prepareSessionCommand(cmd));
	} else if (actionClicked == aRemoveFromIgnoreList) {
		Command_RemoveFromList cmd;
		cmd.set_list("ignore");
		cmd.set_user_name(userName.toStdString());
		
		client->sendCommand(client->prepareSessionCommand(cmd));
	} else if (actionClicked == aKick) {
		Command_KickFromGame cmd;
		cmd.set_player_id(playerId);
		game->sendGameCommand(cmd);
	}
	
	delete menu;
	delete aUserName;
	delete aDetails;
	delete aChat;
	delete aAddToBuddyList;
	delete aRemoveFromBuddyList;
	delete aAddToIgnoreList;
	delete aRemoveFromIgnoreList;
	delete aKick;
}
