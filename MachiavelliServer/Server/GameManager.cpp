#include "GameManager.h"
#include "NetworkServices.h"

GameManager::GameManager() : index_king(0)
{
	// Init card names
	player_card_names.push_back("Builder");
	player_card_names.push_back("Condottiere");
	player_card_names.push_back("King");
	player_card_names.push_back("Magician");
	player_card_names.push_back("Merchant");
	player_card_names.push_back("Murderer");
	player_card_names.push_back("Preacher");
	player_card_names.push_back("Thief");
}

void GameManager::RunServer()
{
	networkServices = std::make_shared<NetworkServices>(std::unique_ptr<GameManager>(this));

	if (networkServices->StartServer())
	{
		// Starting server was success, now listen for clients
		networkServices->ListenForClients();
	}
	else
	{
		std::cerr << "Error starting the Machiavelli server";
	}
}

void GameManager::Start(std::shared_ptr<Player> player_called_start)
{
	// Init decks
	building_card_deck = Parser::LoadBuildingFile();

	// Shuffle the decks
	building_card_deck.Shuffle();
		
    // Send message of starting game
	networkServices->WriteToAllClients("\n");
    networkServices->WriteToAllClients("Starting the game!\n");

    // Set the round
    current_round = 1;

	// Start the game rounds
	while (!IsGameFinished())
	{
		// Load the player deck
		player_card_deck = Parser::LoadCharacterFile();

		// Set the number of player cards
		number_of_player_cards = player_card_deck.Size();

		// Shuffle player cards
		player_card_deck.Shuffle();

		// Let the player choose a card
		for (int i = 0; i < players.size(); i++)
		{
			// Clear the cards of the player
			players.at(i)->ClearPlayerCards();
		}
		GetPlayerCard();

		// Start round
		StartRound();
	}
}

void GameManager::GetPlayerCard()
{
	// Remove the first character card of the deck
	player_card_deck.RemoveLast();

    for (int t = 0; t < 2; t++)
    {
        int counter = index_king;
		for (int i = 0; i < players.size(); i++)
		{
            std::shared_ptr<Player> player;
            
            // Set Player
            if (counter - i <= 0)
                player = players.at(i);
            else
                player = players.at(counter - i);

            // Get the socket
            std::shared_ptr<Socket> socket = player->GetSocket();
            
            // Let the player choose a card
            bool card_set = false;
            while (!card_set)
            {
                // print cards
                PrintPlayerCardDeck(socket);
                
                // Let the user pick a card
                networkServices->WriteToClient("Choose your card\n", socket, true);
                std::string card_name = Utils::ToLowerCase(networkServices->PromptClient(player));
                
                // Check if this card exists

				PlayerCardType card_type = CheckCardType(card_name);
				if (card_type != PlayerCardType::NONE)
				{
					AddCard(card_type, player);
					card_set = true;
					networkServices->WriteToClient("You have chosen: " + card_name + "\n", socket);
				}
				else
					networkServices->WriteToClient("This is not a valid card \n", socket);
            }
            
            // Let the player remove a card from the deck
            if ((i != index_king || (i == index_king && t != 0)) && i < 3)
            {
                bool card_removed = false;
                while (!card_removed)
                {
                    // print cards
                    PrintPlayerCardDeck(socket);
                    
                    // Let the user pick a card
                    networkServices->WriteToClient("Choose the card you want to remove from the deck\n", socket, true);
                    std::string card_name = Utils::ToLowerCase(networkServices->PromptClient(player));
                    
                    // Check if this card exists
					PlayerCardType card_type = CheckCardType(card_name);
					if (card_type != PlayerCardType::NONE)
					{
						RemoveCard(card_type);
						card_removed = true;
					}
					else
                        networkServices->WriteToClient("This is not a valid card \n", socket);
                }
            }
            
            // remove the last card, no need in letting the player do it
            if (i > 3)
            {
                player_card_deck.RemoveLast();
                networkServices->WriteToClient("Removed last card", socket);
            }
            else
            {
                // Else write that the other player is picking cards
                networkServices->WriteToClient("The other player is picking cards\n", socket);
                // Shuffle the deck
                player_card_deck.Shuffle();
            }
        }
    }
    
    // Write to all clients the cards have been picked
    networkServices->WriteToAllClients("The character cards have been picked.\n");
}

void GameManager::StartRound()
{
    networkServices->WriteToAllClients("\n\nStarting round " + std::to_string(current_round) + "\n");
	for (int x = 0; x < number_of_player_cards; x++)
	{
		if (PlayerCardType(x) != killed_player)
		{
			for (int y = 0; y < players.size(); y++)
			{
				std::shared_ptr<Player> player = players.at(y);
				if (player->ContainsPlayerCard(PlayerCardType(x), true))
				{
					if (PlayerCardType(x) == PlayerCardType::KING)
						index_king = y;

					networkServices->WriteToAllClients("It's " + player->GetName() + "'s turn \n");
					Turn(player);
				}
			}
		}
	}
    networkServices->WriteToAllClients("\n Round " + std::to_string(current_round) + " is finished\n");
    current_round++;
}

void GameManager::Turn(std::shared_ptr<Player> player)
{
	// Check if the player is robbed
	if (player->GetCurrentPlayerCard()->GetType() == robbed_player)
		RobPlayer(player);

    // Set used characteristic to false
    player->SetUsedCharacteristic(false);
    
    std::shared_ptr<Socket> socket = player->GetSocket();
    networkServices->WriteToClient("<line>\n", socket);
    
    // Show player info
    networkServices->WriteToClient(player->GetPlayerInfo(), socket);
    
    // Show the options of taking gold or building card
    ShowPlayerOptions(player);
    
    networkServices->WriteToClient("You can now construct a building \n", socket);
    // Show the options for constructing building
    ShowBuildingOptions(player);
    
    // Character specific options
}

void GameManager::ShowPlayerOptions(std::shared_ptr<Player> player)
{
    std::shared_ptr<Socket> socket = player->GetSocket();
    
    bool option_chosen = false;
    while (!option_chosen)
    {
        std::string player_options;
        player_options.append("It's your turn, you have the following options to choose from: \n");
        player_options.append("\t 1: Receive two gold \n");
        player_options.append("\t 2: Take building card \n");
        
        if (!player->HasUsedCharacteristic())
             player_options.append("\t 3: Use characteristic \n");
        
        player_options.append("Please choose one of the given options \n");
        networkServices->WriteToClient(player_options, socket, true);
        
        std::string chosen_option = networkServices->PromptClient(player);
        if (chosen_option == "1")
        {
            // Add the two gold
            player->AddGold(2);
            
            networkServices->WriteToClient("You have chosen to receive two gold. You now have " + std::to_string(player->GetGold()) + " gold.\n", socket);
            option_chosen = true;
        }
        else if (chosen_option == "2")
        {
            std::vector<std::shared_ptr<BuildCard>> buildCards =
            {
                building_card_deck.Pop(),
                building_card_deck.Pop()
            };
            std::string choice_str = "\n";
            choice_str.append("\t 1: " + buildCards[0]->GetName() + "\n");
            choice_str.append("\t 2: " + buildCards[1]->GetName() + "\n");
            choice_str.append("Pick one of the two given cards to add to your building cards\n");
            
            bool building_chosen = false;
            while (!building_chosen)
            {
                networkServices->WriteToClient(choice_str, socket, true);
                
                std::string chosen_card = networkServices->PromptClient(player);
                
                if (chosen_card == "1")
                {
                    player->AddBuildCard(buildCards[0]);
                    
                    chosen_card = buildCards[0]->GetName();
                    networkServices->WriteToClient("You have chosen: " + chosen_card + "\n", socket);
                    building_chosen = true;
                }
                else if (chosen_card == "2")
                {
                    player->AddBuildCard(buildCards[1]);
                    
                    chosen_card = buildCards[1]->GetName();
                    networkServices->WriteToClient("You have chosen: " + chosen_card + "\n", socket);
                    building_chosen = true;
                }
                else
                    networkServices->WriteToClient("This is not a valid option\n", socket);
            }
            option_chosen = true;
        }
		else if (chosen_option == "3" && !player->HasUsedCharacteristic())
		{
			std::shared_ptr<PlayerCard> card = player->GetCurrentPlayerCard();
			card->PerformCharacteristic(shared_from_this(), player);
		}
        else
            networkServices->WriteToClient("\n This isn't a valid option, please try again \n", socket);
    }
}

void GameManager::ShowBuildingOptions(std::shared_ptr<Player> player)
{
    std::shared_ptr<Socket> socket = player->GetSocket();
	
    bool card_chosen = false;
	while (!card_chosen || !player->HasUsedCharacteristic())
	{
		// Create the string with all the cards
		std::vector<std::shared_ptr<BuildCard>> build_cards = player->GetAllBuildCards();
		std::string player_build_cards = "Available cards: \n\n";
		for (int i = 0; i < build_cards.size(); i++)
		{
			player_build_cards.append("\t" + std::to_string(i + 1) + ": " + build_cards[i]->GetName() + "\n");
		}

		if (!player->HasUsedCharacteristic())
			player_build_cards.append("\t0: Use characteristic \n");

		player_build_cards.append("\tStop: Stop your turn.\n");
		player_build_cards.append("Pick one of the given cards \n");

		// Let the player know his choices
		networkServices->WriteToClient(player_build_cards, socket, true);

		// Let the player make a choice
		std::string choice = Utils::ToLowerCase(networkServices->PromptClient(player));
		if (choice == "stop")
			return;
		else
		{
			std::string trim_choice = Utils::trim(choice);

			int iChoice = -1;
			try
			{
				iChoice = std::stoi(choice);
			}
			catch (std::invalid_argument &e)
			{
				// invalid parse
				networkServices->WriteToClient("This is not a valid option\n", socket);
				continue;
			}
			catch (std::out_of_range &e)
			{
				// overflow
				networkServices->WriteToClient("This is not a valid option\n", socket);
				continue;
			}

			if (iChoice == 0)
			{
				// user characteristics
				std::shared_ptr<PlayerCard> card = player->GetCurrentPlayerCard();
				card->PerformCharacteristic(shared_from_this(), player);
			}

			if (iChoice > 0)
			{
				if (iChoice <= build_cards.size())
				{
					std::shared_ptr<BuildCard> card = build_cards[iChoice - 1];
					bool build_success = player->ConstructBuilding(card);
					if (build_success)
					{
						networkServices->WriteToClient("You have built a '" + card->GetName() + "'\n", socket);
						networkServices->WriteToAllExceptCurrent(player->GetName() + " has built " + card->GetName() + "\n", player);
						card_chosen = true;
					}
					else
					{
						networkServices->WriteToClient("Unable to build " + card->GetName() + ", you have insufficient funds.\n", socket);
					}
				}
				else
					networkServices->WriteToClient("This is not a valid option\n", socket);
			}
		}
	}    
}

void GameManager::EndGame()
{

}

bool GameManager::CheckBuildingCard(std::string card_name, std::shared_ptr<Player> player)
{
    std::vector<std::shared_ptr<BuildCard>> building_cards = player->GetAllBuildCards();
    for (int i = 0; i < building_cards.size(); i++)
    {
        if (card_name == building_card_deck.Get(i)->GetName())
        {
            // add or remove building card
            return true;
        }
    }
    return false;
}

PlayerCardType GameManager::CheckCardType(std::string card_name) const
{
	if (card_name == "builder")
		return PlayerCardType::BUILDER;
	else if (card_name == "condottiere")
		return PlayerCardType::CONDOTTIERE;
	else if (card_name == "king")
		return PlayerCardType::KING;
	else if (card_name == "magician")
		return PlayerCardType::MAGICIAN;
	else if (card_name == "merchant")
		return PlayerCardType::MERCHANT;
	else if (card_name == "murderer")
		return PlayerCardType::MURDERER;
	else if (card_name == "preacher")
		return PlayerCardType::PREACHER;
	else if (card_name == "thief")
		return PlayerCardType::THIEF;
	else
		return PlayerCardType::NONE;
}

std::shared_ptr<BuildCard> GameManager::TakeCard()
{
	return building_card_deck.Pop();
}

std::vector<std::shared_ptr<BuildCard>> GameManager::TakeCards(int amount)
{
	std::vector<std::shared_ptr<BuildCard>> cards;

	for (int i = 0; i < amount; i++)
	{
		cards.push_back(building_card_deck.Pop());
	}

	return cards;
}

void GameManager::AddCard(PlayerCardType type, std::shared_ptr<Player> player)
{
	for (int i = 0; i < player_card_deck.Size(); i++)
	{
		if (player_card_deck.Get(i)->GetType() == type)
		{
			player->AddPlayerCard(player_card_deck.Get(i));
			player_card_deck.Remove(i);
			break;
		}
	}
}

void GameManager::RemoveCard(PlayerCardType type)
{
	for (int i = 0; i < player_card_deck.Size(); i++)
	{
		if (player_card_deck.Get(i)->GetType() == type)
		{
			player_card_deck.Remove(i);
			break;
		}
	}
}

std::shared_ptr<Player> GameManager::GetPlayer(int i) const
{
	if (players.size() > i)
		return players.at(i);
	else
		return nullptr;
}

std::vector<std::shared_ptr<Player>> GameManager::GetPlayers() const
{
	return players;
}

int GameManager::GetPlayerAmount() const
{
    return (int)players.size();
}

std::shared_ptr<NetworkServices> GameManager::GetNetworkServices() const
{
	return networkServices;
}

std::vector<std::string> GameManager::GetPlayerCardNames() const
{
	return player_card_names;
}

std::shared_ptr<Player> GameManager::AddPlayer(std::string name, std::shared_ptr<Socket> socket)
{
	if (name.empty())
		name = &"Player" [ players.size()];

	// Add the player
    std::shared_ptr<Player> player = std::make_shared<Player>(name);
    player->SetSocket(socket);
	players.push_back(player);
    
    return player;
}

void GameManager::PrintPlayerCardDeck(std::shared_ptr<Socket> socket)
{
	std::string availableCards = "Available cards:\n\n";
	for (int i = 0; i < player_card_deck.Size(); i++)
	{
		availableCards.append(player_card_deck.Get(i)->GetName() + "\n");
	}
	availableCards.append("\n");
	networkServices->WriteToClient(availableCards, socket);
}

void GameManager::PrintBuildingCardDeck(std::shared_ptr<Socket> socket)
{
    std::string availableCards = "Available Cards: \n\n";
    for (int i = 0; i < building_card_deck.Size(); i++)
    {
        std::shared_ptr<BuildCard> card = building_card_deck.Get(i);
        availableCards.append(card->GetName() + " (" + std::to_string(card->GetCost())
                              + " " + card->GetColorString() + ")" + "\n");
    }
    availableCards.append("\n");
    networkServices->WriteToClient(availableCards, socket);
}

void GameManager::RobPlayer(std::shared_ptr<Player> player)
{
	std::shared_ptr<Player> thief;

	// Find the thief
	for (int x = 0; x < players.size(); x++)
	{
		std::vector<std::shared_ptr<PlayerCard>> player_cards = players.at(x)->GetPlayerCards();
		for (int y = 0; y < player_cards.size(); y++)
		{
			if (player_cards.at(y)->GetType() == PlayerCardType::THIEF)
			{
				thief = players.at(x);
				break;
			}
		}
	}

	// Rob the robbed player
	int gold = player->GetGold();
	player->RemoveGold(gold);
	thief->AddGold(gold);

	// Let the players know
	networkServices->WriteToAllClients(thief->GetName() + " robbed " + player->GetName() + " from his gold (" + std::to_string(gold) + " gold).\n");
}

void GameManager::GameFinished()
{

}

bool GameManager::IsGameFinished()
{
	for (int i = 0; i < players.size(); i++)
	{
		std::vector<std::shared_ptr<BuildCard>> build_cards = players.at(i)->GetBuildedBuildings();
		if (build_cards.size() >= 8)
		{
			return true;
		}
	}
	return false;
}

void GameManager::SetKilledPlayer(PlayerCardType type)
{
	killed_player = type;
}

void GameManager::SetRobbedPlayer(PlayerCardType type)
{
	robbed_player = type;
}

void GameManager::AddBuildCard(std::shared_ptr<BuildCard> build_card)
{
	building_card_deck.Add(build_card);
	building_card_deck.Shuffle();
}

GameManager::~GameManager()
{
}