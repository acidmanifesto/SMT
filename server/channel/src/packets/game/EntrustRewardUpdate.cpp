/**
 * @file server/channel/src/packets/game/EntrustRewardUpdate.cpp
 * @ingroup channel
 *
 * @author HACKfrost
 *
 * @brief Request from the client to update the reward items given out
 *  upon entrust complete.
 *
 * This file is part of the Channel Server (channel).
 *
 * Copyright (C) 2012-2020 COMP_hack Team <compomega@tutanota.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Packets.h"

// libcomp Includes
#include <Log.h>
#include <ManagerPacket.h>
#include <Packet.h>
#include <PacketCodes.h>

// object Includes
#include <Item.h>
#include <ItemBox.h>
#include <MiItemBasicData.h>
#include <MiItemData.h>
#include <PlayerExchangeSession.h>

// channel Includes
#include "ChannelServer.h"
#include "CharacterManager.h"
#include "DefinitionManager.h"
#include "ManagerConnection.h"

using namespace channel;

bool Parsers::EntrustRewardUpdate::Parse(
    libcomp::ManagerPacket* pPacketManager,
    const std::shared_ptr<libcomp::TcpConnection>& connection,
    libcomp::ReadOnlyPacket& p) const {
  if (p.Size() != 16) {
    return false;
  }

  int64_t itemID = p.ReadS64Little();
  int32_t rewardType = p.ReadS32Little();
  int32_t offset = p.ReadS32Little();

  auto server =
      std::dynamic_pointer_cast<ChannelServer>(pPacketManager->GetServer());
  auto characterManager = server->GetCharacterManager();
  auto client = std::dynamic_pointer_cast<ChannelClientConnection>(connection);
  auto state = client->GetClientState();
  auto cState = state->GetCharacterState();
  auto character = cState->GetEntity();
  auto inventory = character->GetItemBoxes(0).Get();
  auto exchangeSession = state->GetExchangeSession();

  auto otherClient = exchangeSession && exchangeSession->GetSourceEntityID() !=
                                            cState->GetEntityID()
                         ? server->GetManagerConnection()->GetEntityClient(
                               exchangeSession->GetSourceEntityID(), false)
                         : nullptr;

  auto item = itemID != -1 ? std::dynamic_pointer_cast<objects::Item>(
                                 libcomp::PersistentObject::GetObjectByUUID(
                                     state->GetObjectUUID(itemID)))
                           : nullptr;
  auto itemDef =
      item ? server->GetDefinitionManager()->GetItemData(item->GetType())
           : nullptr;

  bool success = false;
  if (item &&
      (!itemDef || (itemDef->GetBasic()->GetFlags() & ITEM_FLAG_TRADE) == 0)) {
    LogTradeError([item, state]() {
      return libcomp::String(
                 "Player attempted to add non-trade item type %1 to an entrust "
                 "reward: %2\n")
          .Arg(item->GetType())
          .Arg(state->GetAccountUID().ToString());
    });
  } else if ((itemID == -1 ||
              (item && item->GetItemBox() == inventory->GetUUID())) &&
             otherClient) {
    success = true;

    if (item) {
      // Add to slots 10 to 21, make sure the item is not already there
      auto items = exchangeSession->GetItems();
      for (size_t i = 10; i < 22; i++) {
        if (items[i].Get() == item) {
          LogTradeError([state]() {
            return libcomp::String(
                       "Player attempted to add a synthesis reward item more "
                       "than once: %1\n")
                .Arg(state->GetAccountUID().ToString());
          });

          success = false;
          break;
        }
      }
    }

    if (success) {
      exchangeSession->SetItems((size_t)(10 + rewardType * 4 + offset), item);
    }
  }

  libcomp::Packet reply;
  reply.WritePacketCode(
      ChannelToClientPacketCode_t::PACKET_ENTRUST_REWARD_UPDATE);
  reply.WriteS64Little(itemID);
  reply.WriteS32Little(rewardType);
  reply.WriteS32Little(offset);
  reply.WriteS32Little(success ? 0 : -1);

  client->SendPacket(reply);

  if (success) {
    auto otherState = otherClient->GetClientState();
    int64_t otherItemID = item ? otherState->GetObjectID(item->GetUUID()) : -1;

    libcomp::Packet notify;
    notify.WritePacketCode(
        ChannelToClientPacketCode_t::PACKET_ENTRUST_REWARD_UPDATED);
    notify.WriteS32Little(rewardType);
    notify.WriteS32Little(offset);
    notify.WriteS64Little(otherItemID);

    characterManager->GetItemDetailPacketData(notify, item);

    otherClient->SendPacket(notify);
  }

  return true;
}
