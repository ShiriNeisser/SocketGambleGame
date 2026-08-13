# SocketGambleGame — סיכום ארכיטקטורה

> **לתיעוד המלא והמפורט** (פרוטוקול, סנריו הרצה, mutexes, לקוח, ועוד) — ראה **[DOCS.md](./DOCS.md)**.

מסמך זה מסכם את ליבת השרת: **Reactor אחד מבוסס epoll** (Producer) שמזין **Thread Pool** (Consumer) עם תור מעגלי bounded.

**גרסת קוד:** `main` @ `46748ca`

---

## תמצית

| רכיב | תפקיד |
|------|--------|
| **Epoll thread** | accept/recv לא-חוסם → `pool_submit()` |
| **Ring buffer** | 2048 slots, head/tail/count |
| **Workers (N≥4)** | AUTH, BET, GAME, FINAL, DISCONNECT |
| **Game thread** | סימולציה + UDP goal broadcast |
| **Broadcast thread** | countdown UDP |

## HEAD / TAIL

```
Producer → queue[tail++]   (pool_submit)
Consumer → queue[head++]   (worker_main)
Full (count=2048) → Producer חוסם → backpressure על epoll
```

## צוואר בקבוק עיקרי

מספר workers (min 4) תחת burst — לא גודל התור.

---

ראה [DOCS.md](./DOCS.md) לפרטים מלאים.
