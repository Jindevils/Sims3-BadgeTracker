using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Sims3.Gameplay;
using Sims3.Gameplay.DynamicChallenges;
using Sims3.SimIFace;
using Sims3.UI;

[assembly: Tunable]

namespace Ts3Conquistas
{
    internal static class ShortcutInput
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ConsumeShortcut();

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void BeginBadges();
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void BadgeUnit(int unit);
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void EndBadge(int status);
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void ShowBadges();
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void HideBadges();

        internal static void Field(string text)
        {
            if (text != null)
                foreach (char unit in text)
                    BadgeUnit(unit);
            BadgeUnit(-1);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static bool Consume()
        {
            return ConsumeShortcut() != 0;
        }
    }

    public class Bootstrap
    {
        [Tunable]
        protected static bool init = false;
        private static bool panelOpen;
        private static bool panelQueued;
        private static bool bridgeReady;
        private static Window listener;
        private static uint worldGeneration;
        private static readonly Dictionary<Guid, bool> notified = new Dictionary<Guid, bool>();

        static Bootstrap()
        {
            World.OnWorldLoadFinishedEventHandler += OnWorldLoaded;
            World.OnWorldQuitEventHandler += OnWorldQuit;
        }

        private static void OnWorldLoaded(object sender, EventArgs args)
        {
            CommandSystem.UnregisterCommand("conquistas");
            CommandSystem.RegisterCommand("conquistas", "Abre o painel de conquistas locais", OpenCommand);
            StopListening();
            notified.Clear();
            DynamicChallengeManager.AchievementCompletionEvent -= OnAchievementCompleted;
            DynamicChallengeManager.AchievementCompletionEvent += OnAchievementCompleted;
            listener = UIManager.GetMainWindow();
            if (listener != null)
                listener.Tick += OnTick;
        }



        private static void OnWorldQuit(object sender, EventArgs args)
        {
            CommandSystem.UnregisterCommand("conquistas");
            DynamicChallengeManager.AchievementCompletionEvent -= OnAchievementCompleted;
            if (bridgeReady) ShortcutInput.HideBadges();
            StopListening();
        }

        private static void OnAchievementCompleted(object sender, DynamicChallenge.AchievementCompleteEventArgs args)
        {
            if (!bridgeReady || args == null || notified.ContainsKey(args.mChallengeGuid))
                return;
            try
            {
                var db = DynamicChallengeDatabase.Instance;
                if (db == null || !db.CompletedContainsGuid(args.mChallengeGuid))
                    return;
                string name = db.GetBadgeNameForID(args.mChallengeGuid);
                if (string.IsNullOrEmpty(name))
                    return;
                ShortcutInput.BeginBadges();
                ShortcutInput.Field(name);
                ShortcutInput.Field(StringTable.GetLocale());
                ShortcutInput.Field(db.GetMedalTypeForID(args.mChallengeGuid).ToString());
                ShortcutInput.Field(args.mChallengeGuid.ToString());
                ShortcutInput.EndBadge(4);
                ShortcutInput.ShowBadges();
                notified[args.mChallengeGuid] = true;
            }
            catch (ResetException) { throw; }
            catch (Exception) { }
        }

        private static void StopListening()
        {
            if (listener != null)
                listener.Tick -= OnTick;
            listener = null;
            worldGeneration++;
            panelQueued = false;
        }

        private static void OnTick(WindowBase sender, UIEventArgs args)
        {
            bool pressed;
            try
            {
                pressed = ShortcutInput.Consume();
                bridgeReady = true;
            }
            catch (Exception)
            {
                StopListening();
                return;
            }

            if (!pressed || panelQueued || panelOpen || UIManager.IsModal())
                return;

            panelQueued = true;
            uint generation = worldGeneration;
            Simulator.AddObject(new Sims3.Gameplay.OneShotFunctionTask(delegate
            {
                if (generation != worldGeneration)
                    return;
                panelQueued = false;
                if (!UIManager.IsModal())
                    OpenCommand(null);
            }));
        }

        private static int OpenCommand(object[] args)
        {
            if (panelOpen)
                return 0;

            panelOpen = true;
            try
            {
                if (!bridgeReady)
                    return ShowPanel() ? 0 : 1;
                PublishOverlay();
                return 0;
            }
            finally
            {
                panelOpen = false;
            }
        }
        private sealed class Badge
        {
            public Guid Id;
            public string Name;
            public string Description;
            public string Themes;
            public string Status;
            public DynamicChallenge Challenge;
        }

        private static List<Badge> ReadBadges()
        {
            var db = DynamicChallengeDatabase.Instance;
            var manager = DynamicChallengeManager.Instance;
            var result = new List<Badge>();
            if (db == null || manager == null)
                return result;

            var loaded = new Dictionary<Guid, DynamicChallenge>();
            if (manager.CurrentChallengesReadOnly != null)
                foreach (var challenge in manager.CurrentChallengesReadOnly)
                    loaded[challenge.ChallengeGuid] = challenge;

            var ids = new Dictionary<Guid, bool>();
            foreach (Guid id in loaded.Keys)
                ids[id] = db.CompletedContainsGuid(id);

            if (db.ChallengesCompleted != null)
                foreach (Guid id in db.ChallengesCompleted)
                    ids[id] = true;

            foreach (var entry in ids)
            {
                DynamicChallenge challenge;
                loaded.TryGetValue(entry.Key, out challenge);
                string name = null;
                string description = null;
                string metadata = "";
                try
                {
                    name = db.GetBadgeNameForID(entry.Key);
                    description = db.GetBadgeDescriptionForID(entry.Key);
                    metadata = db.GetCategoriesForID(entry.Key) + ", " + db.GetMedalTypeForID(entry.Key);
                }
                catch (KeyNotFoundException)
                {
                    // The game can retain a completion without loading its badge metadata.
                }
                if (string.IsNullOrEmpty(name) && challenge != null)
                    name = challenge.DebugName;
                result.Add(new Badge
                {
                    Id = entry.Key,
                    Challenge = challenge,
                    Name = string.IsNullOrEmpty(name) ? entry.Key.ToString() : name,
                    Description = description,
                    Themes = metadata + ", " + (challenge == null ? "" : challenge.MedalType + ", " +
                        (challenge.Themes == null ? "" : string.Join(", ", challenge.Themes))),
                    Status = entry.Value ? "Completa" :
                        challenge.ChallengeState == DynamicChallenge.DynamicChallengeState.Incomplete
                            ? "Incompleta" : "Estado indeterminado"
                });
            }

            result.Sort(delegate(Badge first, Badge second)
            {
                int byName = string.Compare(first.Name, second.Name, true);
                return byName == 0 ? first.Id.CompareTo(second.Id) : byName;
            });
            return result;
        }

        private static string ReadMilestone(Badge badge)
        {
            if (badge.Id != new Guid("de02690e-4eb9-41d3-a08b-c2c1ad4ef57d"))
                return ReadGameCounter(badge.Challenge);

            var household = Sims3.Gameplay.CAS.Household.ActiveHousehold;
            if (household == null)
                return "";

            Sims3.Gameplay.CAS.SimDescription leader = null;
            int mostChildren = -1;
            foreach (var sim in household.AllSimDescriptions)
            {
                if (sim == null || !sim.IsHuman || sim.Genealogy == null ||
                    sim.Genealogy.Children == null)
                    continue;
                int children = sim.Genealogy.Children.Count;
                if (children > mostChildren)
                {
                    leader = sim;
                    mostChildren = children;
                }
            }
            if (leader == null)
                return "";

            // This final metadata field can contain spaces or punctuation in Sim names.
            return ", Milestone:" + mostChildren + "/15/children\n" + leader.FullName;
        }

        private const System.Reflection.BindingFlags CounterFields =
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;

        private static IDictionary<ulong, ChallengeNode> ReadNodes(
            Type type, string fieldName, DynamicChallengeLeafCircumstance leaf)
        {
            var field = type.GetField(fieldName, CounterFields);
            return field == null ? null : field.GetValue(leaf) as IDictionary<ulong, ChallengeNode>;
        }

        private static string ReadGameCounter(DynamicChallenge challenge)
        {
            if (challenge == null)
                return "";
            var leaf = challenge.SuccessCircumstances as DynamicChallengeLeafCircumstance;
            if (leaf == null)
            {
                var field = typeof(DynamicChallenge).GetField("mSuccessCircumstanceArray", CounterFields);
                var leaves = field == null ? null : field.GetValue(challenge) as DynamicChallengeLeafCircumstance[];
                if (leaves == null || leaves.Length != 1)
                    return "";
                leaf = leaves[0];
            }
            if (leaf == null || leaf.NecessaryCompletions <= 1 ||
                leaf.NecessaryCompletions > 1000000000)
                return "";

            var household = Sims3.Gameplay.CAS.Household.ActiveHousehold;
            ChallengeNode node = null;
            string subject = "";
            if (leaf is DynamicChallengeSimCircumstance && household != null)
            {
                var nodes = ReadNodes(typeof(DynamicChallengeSimCircumstance), "mSimDictionary", leaf);
                if (nodes == null)
                    return "";
                foreach (var sim in household.AllSimDescriptions)
                {
                    ChallengeNode candidate;
                    if (sim != null && nodes.TryGetValue(sim.SimDescriptionId, out candidate) &&
                        candidate != null && (node == null || candidate.CompletionCount > node.CompletionCount))
                    {
                        node = candidate;
                        subject = sim.FullName;
                    }
                }
            }
            else if (leaf is DynamicChallengeHouseholdCircumstance && household != null)
            {
                var nodes = ReadNodes(typeof(DynamicChallengeHouseholdCircumstance), "mHouseholdDictionary", leaf);
                if (nodes != null)
                    nodes.TryGetValue(household.HouseholdId, out node);
                subject = household.Name;
            }
            else if (leaf is DynamicChallengePlayerCircumstance)
            {
                var field = typeof(DynamicChallengePlayerCircumstance).GetField("mNode", CounterFields);
                if (field != null)
                    node = field.GetValue(leaf) as ChallengeNode;
            }
            if (node == null)
                return "";

            return ", Milestone:" + Math.Min(node.CompletionCount, leaf.NecessaryCompletions) +
                "/" + leaf.NecessaryCompletions + "/count\n" + subject;
        }

        private static uint InstalledPacks()
        {
            var expansions = new ProductVersion[] {
                ProductVersion.EP1, ProductVersion.EP2, ProductVersion.EP3,
                ProductVersion.EP4, ProductVersion.EP5, ProductVersion.EP6,
                ProductVersion.EP7, ProductVersion.EP8, ProductVersion.EP9,
                ProductVersion.EP10, ProductVersion.EP11
            };
            var stuff = new ProductVersion[] {
                ProductVersion.SP1, ProductVersion.SP2, ProductVersion.SP3,
                ProductVersion.SP4, ProductVersion.SP5, ProductVersion.SP6,
                ProductVersion.SP7, ProductVersion.SP8, ProductVersion.SP9
            };
            uint mask = 1u << 1;
            for (int i = 0; i < expansions.Length; ++i)
                if (GameUtils.IsInstalled(expansions[i])) mask |= 1u << (i + 2);
            for (int i = 0; i < stuff.Length; ++i)
                if (GameUtils.IsInstalled(stuff[i])) mask |= 1u << (i + 13);
            return mask;
        }

        private static void PublishOverlay()
        {
            var badges = ReadBadges();
            ShortcutInput.BeginBadges();
            ShortcutInput.Field(StringTable.GetLocale());
            ShortcutInput.Field(InstalledPacks().ToString(System.Globalization.CultureInfo.InvariantCulture));
            ShortcutInput.Field("");
            ShortcutInput.Field("");
            ShortcutInput.EndBadge(3);
            foreach (Badge badge in badges)
            {
                ShortcutInput.Field(badge.Name);
                ShortcutInput.Field(badge.Description);
                ShortcutInput.Field(badge.Themes + ReadMilestone(badge));
                ShortcutInput.Field(badge.Id.ToString());
                ShortcutInput.EndBadge(badge.Status == "Completa" ? 1 :
                    badge.Status == "Incompleta" ? 0 : 2);
            }
            ShortcutInput.ShowBadges();
        }

        private static bool ShowPanel()
        {
            try
            {
                var badges = ReadBadges();
                if (badges.Count == 0)
                {
                    SimpleMessageDialog.Show("Conquistas locais",
                        "O jogo ainda n?o disponibilizou conquistas para esta sess?o.");
                    return true;
                }

                var all = new List<ObjectPicker.RowInfo>();
                var complete = new List<ObjectPicker.RowInfo>();
                var incomplete = new List<ObjectPicker.RowInfo>();
                foreach (Badge badge in badges)
                {
                    var row = new ObjectPicker.RowInfo(badge, new List<ObjectPicker.ColumnInfo>
                    {
                        new ObjectPicker.TextColumn(badge.Name),
                        new ObjectPicker.TextColumn(badge.Status)
                    });
                    all.Add(row);
                    if (badge.Status == "Completa")
                        complete.Add(row);
                    else if (badge.Status == "Incompleta")
                        incomplete.Add(row);
                }

                var tabs = new List<ObjectPicker.TabInfo>
                {
                    new ObjectPicker.TabInfo("shop_all_r2", "Todas (" + all.Count + ")", all),
                    new ObjectPicker.TabInfo("shop_all_r2", "Completas (" + complete.Count + ")", complete),
                    new ObjectPicker.TabInfo("shop_all_r2", "Incompletas (" + incomplete.Count + ")", incomplete)
                };
                var headers = new List<ObjectPicker.HeaderInfo>
                {
                    new ObjectPicker.HeaderInfo("Conquista", "T?tulo no jogo", 325),
                    new ObjectPicker.HeaderInfo("Estado", "Estado reconhecido pelo jogo", 155)
                };

                while (true)
                {
                    var selected = ObjectPickerDialog.Show("Conquistas locais",
                        "Detalhes", "Fechar", tabs, headers, 1, null, true);
                    if (selected == null || selected.Count == 0)
                        return true;

                    var badge = (Badge)selected[0].Item;
                    string description = string.IsNullOrEmpty(badge.Description)
                        ? "Descri??o indispon?vel nesta sess?o."
                        : badge.Description;
                    string themes = string.IsNullOrEmpty(badge.Themes) ? "" : "\n" + badge.Themes;
                    SimpleMessageDialog.Show(badge.Name, badge.Status + "\n\n" + description
                        + themes + "\n\n" + badge.Id);
                }
            }
            catch (ResetException)
            {
                throw;
            }
            catch (Exception ex)
            {
                SimpleMessageDialog.Show("Conquistas locais", "Falha ao abrir o painel.\n\n" + ex.Message);
                return false;
            }
        }

    }
}





