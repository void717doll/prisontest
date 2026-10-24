#pragma once
#include "common.hpp"
#include <deque>
#include <random>
namespace prison {
constexpr int VaultPageSize = 54;
constexpr int MaxVaultPages = 25;
struct TimedMultiplier {
    double amount = 1, remaining = 0;
};
class Game {
  public:
    explicit Game(const Data& source);
    const Data& data;
    Json state;
    std::mt19937_64 random{std::random_device{}()};
    void newWorld(bool devmode = false);
    void loadSnapshot(const Json& snapshot);
    Json snapshot() const;
    std::string toolName() const;
    bool ownsItem(const Json& item) const;
    bool giveDrop(Json item, bool automatic = false);
    void loadActivity();
    void tickActivity(double seconds);
    Result activityAction(const std::string& action, const std::string& value = "");
    Result castFishing(Vec3 point);
    Result tendPlot(int index, bool water);
    double cropProgress(int index) const;
    void awardActivity(double value, double experience, int count);
    Json captureRewardContext() const;
    MiningReward mineBlock(const std::string& blockId, bool allowProcs = true, bool manual = true,
                           const std::string& source = "manual", const Json& context = Json());
    void finishProc(int virtualBlocks, int compressedBlocks, const Json& context = Json());
    void awardBomb(int removed, int samples, int side, const Json& context, const Json& sample);
    void tick(double seconds);
    int tickAutominer(double seconds, const Json& sample);
    int autominerTickCost() const;
    Result rankUp(bool maximum = false);
    Result prestige(bool confirmed = false);
    Result rebirth(bool confirmed = false);
    Result buyEnchant(const std::string& id, int count = 1);
    void loadTutorial();
    int tutorialStep() const;
    void tutorialEvent(const std::string& event);
    Result tutorialAction(const std::string& action);
    Result prestigeParts();
    double boost(const std::string& kind) const;
    TimedMultiplier activeMultiplier(const std::string& kind) const;
    double pickXpNeeded(int level) const;
    int crystalSlots() const;
    const Json& crystalResonance() const;
    const Json& petMutation(const Json& pet) const;
    double mutationBonus(const std::string& kind) const;
    void rollPetMutation(Json& pet);
    void loadGuardian();
    bool spawnGuardian(Vec3 position, bool development = false);
    bool tickGuardian(double seconds);
    Result strikeGuardian(int part);
    double rankCost(int index) const;
    double prestigeCost() const;
    double enchantCost(const std::string& id, int count = 1) const;
    int enchantLevel(const std::string& id) const;
    std::string enchantCurrency(const std::string& id) const;
    std::string currencyName(const std::string& id) const;
    void premiumDrops();
    void awardJackpot();
    Json makeRobot(const std::string& kind = "");
    double robotMultiplier(const Json& robot) const;
    int robotFuelCost(const Json& robot) const;
    Json consumeRobotFuel(int blocks);
    Json robotUpgradeQuote(int index, int levels) const;
    void loadEndgame();
    void tickEndgame(double seconds);
    void rollEndgame(std::vector<Proc>& procs);
    double endgameMultiplier(const std::string& id) const;
    void recordIncome(const MiningReward& reward);
    void addPetExperience(int amount);
    bool giveItem(Json item, bool automatic = false);
    void returnStoredItems();
    Result transfer(const std::string& from, int index, const std::string& to, int target = -1);
    Result useItem(int index);
    Result activateHeld(int index, bool primary);
    Result itemAction(const std::string& action, const Json& args = Json());
    Json multiplierRecipes() const;
    const Json& pointShopProducts() const;
    std::string filterPattern(const Json& item, bool family = false) const;
    Json filterProfile(const Json& settings) const;
    void awardPocketDrops(int count);
    Json mineOptions() const;
    Json mineUpgradeQuote(const std::string& id) const;
    const Json& specialization(const std::string& id) const;
    Json specializationModifiers() const;
    void loadEquipmentSets();
    Result equipmentSetAction(const std::string& action, const Json& args);
    void loadShaft();
    bool inShaft() const;
    bool shaftComplete() const;
    const Json& shaftCondition() const;
    Json shaftFloor(int depth) const;
    Result shaftAction(const std::string& action, const Json& args = Json());
    bool recordShaftMining(int blocks);
    Result progressionAction(const std::string& action, const Json& args);
    void loadProgression();
    void recordMining(int blocks, int ores, const Json& context);
    Json contractChoices() const;
    std::string contractText(const Json& contract) const;
    void recordProgress(const std::string& kind, int count, const Json& context);
    Json expeditionQuote(int inventoryIndex) const;
    int vaultPages() const;
    Json vaultSlots(int page) const;
    std::string vaultName(int page) const;
    double nextVaultCost() const;
    Result command(const std::string& text);
    Result giveTestItem(const Json& fields);
    Result customizeTestPick(const Json& fields);
    void tickItems(double seconds, int blocks = 0, int hatcheryLevel = -1);
    std::string itemName(const Json& item) const;
    std::vector<std::string> itemLore(const Json& item) const;

  private:
    Result itemActionImpl(const std::string& action, const Json& args);
    Result developmentCommand(const std::string& name, const std::vector<std::string>& args);
    Result configureFilter(const std::string& action, const Json& args);
    double autominerElapsed_ = 0;
    struct Income {
        double second = 0, money = 0, tokens = 0;
    };
    std::deque<Income> recentIncome_;
    bool endgameReady(const std::string& id) const;
    bool activateEndgame(const std::string& id, std::vector<Proc>& procs);
};
} // namespace prison
