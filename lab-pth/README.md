# Laboratorium `.pth`

Jednolinijkowy `cook.pth` uruchamia lokalny moduł przy starcie osobnego Pythona 3.12. Moduł wyświetla stałą `FAKE_API_KEY=DEMO-NIE-JEST-SEKRETEM` i zapisuje zdarzenie do `startup-events.jsonl` w tym folderze. Nie odczytuje kluczy, zmiennych środowiskowych, przeglądarek ani plików użytkownika; nie wykonuje połączeń sieciowych. Nie instalujemy LiteLLM ani historycznego malware.

Podwójne kliknięcie `Start-demo.cmd` otwiera pokaz. Pierwszy Enter uruchamia prawdziwy interaktywny interpreter; zakończ go przez `exit()`. Drugi Enter uruchamia porównanie `python -S`; zakończ je przez `raise SystemExit` (`exit()` jest dodawane przez pominięty tu moduł `site`).

Obsługa z bazowego Pythona 3.12 (launcher `py` z python.org):

```powershell
py -3.12 .\lab.py setup
py -3.12 .\lab.py test
py -3.12 .\lab.py cleanup
```

`cleanup` usuwa tylko dwa utworzone pliki haka, po sprawdzeniu identyfikatora i zawartości; nie usuwa rekurencyjnie katalogów. `setup` ponownie go zakłada. Bazowego Pythona nie zmieniamy. `.venv` to osobne środowisko zależności, **nie granica bezpieczeństwa dla złośliwego kodu**. Faktyczne obce próbki wymagają osobnej maszyny/VM z odpowiednią izolacją. Tutaj próbki malware nie ma.

Istotny szczegół: `-I` izoluje konfigurację użytkownika, lecz nie blokuje haka z `site-packages` tego venv. `-S` pomija inicjalizację `site`, co potwierdza test. To wyjaśnienie mechanizmu, a nie ogólna rekomendacja uruchamiania obcych programów z `-S`.
