# Komponenty zewnętrzne

Repozytorium zawiera jeden plik cudzego autorstwa; pozostałe komponenty pobierasz osobno z ich źródeł.

| Komponent | Gdzie | Licencja / warunki |
|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | `plugin/json.hpp` (dołączony) | MIT, nagłówek pliku zawiera informację o prawach |
| [Union API](https://gitlab.com/union-framework/union-api), [Gothic API](https://gitlab.com/union-framework/gothic-api) | pobierane do `third_party/` | warunki w repozytoriach Union Framework |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | `backend/runner/` (nie w repozytorium) | MIT |
| NVIDIA Nemotron 3.5 Bolt E4B (GGUF) | `backend/models/` (nie w repozytorium) | licencja modelu NVIDIA, [developer.nvidia.com/nemotron](https://developer.nvidia.com/nemotron) |
| [VoxCPM2](https://github.com/OpenBMB/VoxCPM) | osobna instalacja (`VOXCPM_HOME`) | warunki projektu OpenBMB |
| Gothic II: Noc Kruka | Twoja zainstalowana kopia gry | gra, jej skrypty, dialogi i nagrania należą do ich właścicieli; nie są tu dołączone |

Adaptery głosu trenowane na nagraniach z gry służą wyłącznie do prywatnych eksperymentów na własnej kopii i nie są publikowane.
