// Valdis Story -> PS Vita: parche de rutas.
// El juego carga sprites/fondos externos con rutas de Windows ("data_5\anim_x.png").
// El runner Android/Vita usa '/', asi que se normalizan todas las cadenas data_N\...
using System.Text.RegularExpressions;

EnsureDataLoaded();
var rx = new Regex(@"^data_\d+\\");
int changed = 0;
foreach (var s in Data.Strings)
{
    if (s?.Content != null && rx.IsMatch(s.Content))
    {
        s.Content = s.Content.Replace('\\', '/');
        changed++;
    }
}
ScriptMessage($"vita_paths: {changed} rutas normalizadas");
