"""Void Gate texts: d2mods/VoidGate/lang/<language>_m.json (merged into strings/<language>_m.lang by d2install).

All eleven languages of the game: English, Russian and nine translations (OTHER). The Far Reach "Cancel" prompt
is shortened in every language. No ASCII double quotes in texts (the .lang format): use typographic quotes.

  py tools\\make_voidgate_lang.py
"""
import json
import os

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "d2mods", "VoidGate", "lang")

EN = {
    "Power_voidgate_name": "Void Gate",
    "Power_voidgate_desc": "Open two linked rifts into the Void. Step into one to emerge from the other.",
    "Power_strategy_voidgate_desc":
        "<p>Each use opens a rift on the surface you look at: first the entrance, then the exit. A new rift replaces "
        "the oldest one. Tap the interact key to close both.</p><br><p>You keep your speed and direction through a "
        "rift: fall into a rift in the floor to fly out of a rift in a wall. If a rift strands you far below its "
        "exit, the interact key also brings you back to where you entered.</p>",
    "Upgrade_power_voidgate_far_name": "Far Rift",
    "Upgrade_power_voidgate_far_desc": "Rifts can be opened twice as far away.",
    "Power_strategy_voidgate_far_desc": "Doubles the distance at which you can open a rift.",
    "Upgrade_power_voidgate_through_name": "Through the Void",
    "Upgrade_power_voidgate_through_desc": "People, bodies and thrown objects pass through rifts.",
    "Power_strategy_voidgate_through_desc":
        "<p>The Void accepts more than you. Guards, bodies, bottles and grenades pass through rifts and keep their "
        "momentum.</p><br><p>Open an exit above a drop and let your pursuers follow you.</p>",
    "Upgrade_power_voidgate_momentum_name": "Crossfire",
    "Upgrade_power_voidgate_momentum_desc": "Bolts, bullets and Windblast pass through rifts.",
    "Power_strategy_voidgate_momentum_desc":
        "<p>Shoot into one rift to hit whoever stands before the other.</p><br><p>Windblast cast into a rift bursts "
        "out of its twin.</p>",
    "ui_statusinteraction_closevoidgate": "Close the gates",
}
RU = {
    "Power_voidgate_name": "Врата Бездны",
    "Power_voidgate_desc": "Открывает два связанных разлома в Бездну. Войдите в один — выйдете из другого.",
    "Power_strategy_voidgate_desc":
        "<p>Каждое применение открывает разлом на поверхности, куда вы смотрите: сначала вход, затем выход. Новый "
        "разлом заменяет самый старый. Клавиша взаимодействия закрывает оба.</p><br><p>Сквозь разлом вы сохраняете "
        "скорость и направление: прыгните в разлом на полу, чтобы вылететь из разлома в стене. Если разлом завёл вас "
        "далеко вниз от выхода, клавиша взаимодействия вернёт вас к месту входа.</p>",
    "Upgrade_power_voidgate_far_name": "Дальний разлом",
    "Upgrade_power_voidgate_far_desc": "Разломы можно открывать вдвое дальше.",
    "Power_strategy_voidgate_far_desc": "Удваивает расстояние, на котором вы можете открыть разлом.",
    "Upgrade_power_voidgate_through_name": "Сквозь Бездну",
    "Upgrade_power_voidgate_through_desc": "Через разломы проходят люди, тела и брошенные предметы.",
    "Power_strategy_voidgate_through_desc":
        "<p>Бездна принимает не только вас. Стражники, тела, бутылки и гранаты проходят сквозь разломы, сохраняя "
        "скорость.</p><br><p>Откройте выход над пропастью — и пусть погоня последует за вами.</p>",
    "Upgrade_power_voidgate_momentum_name": "Перекрёстный огонь",
    "Upgrade_power_voidgate_momentum_desc": "Болты, пули и Порыв ветра проходят сквозь разломы.",
    "Power_strategy_voidgate_momentum_desc":
        "<p>Выстрелите в один разлом, чтобы поразить того, кто стоит перед другим.</p><br><p>Порыв ветра, пущенный "
        "в разлом, вырывается из его близнеца.</p>",
    "ui_statusinteraction_closevoidgate": "Закрыть врата",
}
# The game's other nine languages. Power names, "the Void", ammo and enemy words are the game's own translations
# (strings/<language>_m.lang: Power_windblast_name, MapName_Campaign_Void, Ammo_bolt_regular, ...); the second
# person and the style of each language follow the game's power texts. Chinese is the game's Traditional Chinese.
# The French texts use a no-break space before a colon, as the game's French does. Every character is one the
# game's own text in that language uses (its fonts may hold only those): 交錯火力, not 交叉火力.
OTHER = {
    "brazilian_m": {
        "Power_voidgate_name": "Portal do Vazio",
        "Power_voidgate_desc": "Abre duas fendas ligadas ao Vazio. Entre em uma para sair pela outra.",
        "Power_strategy_voidgate_desc":
            "<p>Cada uso abre uma fenda na superfície para a qual você está olhando: primeiro a entrada, depois a "
            "saída. Uma nova fenda substitui a mais antiga. Toque na tecla de interação para fechar as duas.</p><br>"
            "<p>Você mantém sua velocidade e direção ao atravessar uma fenda: caia em uma fenda no chão para sair "
            "voando de uma fenda na parede. Se uma fenda deixar você muito abaixo da saída, a tecla de interação "
            "também leva você de volta ao ponto de entrada.</p>",
        "Upgrade_power_voidgate_far_name": "Fenda Distante",
        "Upgrade_power_voidgate_far_desc": "As fendas podem ser abertas duas vezes mais longe.",
        "Power_strategy_voidgate_far_desc": "Dobra a distância na qual você pode abrir uma fenda.",
        "Upgrade_power_voidgate_through_name": "Através do Vazio",
        "Upgrade_power_voidgate_through_desc": "Pessoas, cadáveres e objetos arremessados atravessam as fendas.",
        "Power_strategy_voidgate_through_desc":
            "<p>O Vazio aceita mais do que você. Guardas, cadáveres, garrafas e granadas atravessam as fendas e "
            "mantêm o impulso.</p><br><p>Abra uma saída acima de um precipício e deixe que seus perseguidores sigam "
            "você.</p>",
        "Upgrade_power_voidgate_momentum_name": "Fogo Cruzado",
        "Upgrade_power_voidgate_momentum_desc": "Virotes, balas e Rajada de Vento atravessam as fendas.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Atire em uma fenda para acertar quem estiver diante da outra.</p><br><p>Uma Rajada de Vento "
            "conjurada em uma fenda irrompe pela sua gêmea.</p>",
        "ui_statusinteraction_closevoidgate": "Fechar os portais",
    },
    "chinese_m": {
        "Power_voidgate_name": "虛空之門",
        "Power_voidgate_desc": "開啟兩道相連的虛空裂隙。踏入其中一道，便會從另一道走出。",
        "Power_strategy_voidgate_desc":
            "<p>每次施展都會在你注視的表面上開啟一道裂隙：先是入口，然後是出口。新的裂隙會取代最舊的一道。輕按互動鍵"
            "即可關閉兩道裂隙。</p><br><p>穿過裂隙時，你會保持原本的速度和方向：跳進地面上的裂隙，就能從牆上的裂隙"
            "飛出。如果裂隙讓你落到出口下方很遠的地方，按下互動鍵也能讓你回到進入的位置。</p>",
        "Upgrade_power_voidgate_far_name": "遠距裂隙",
        "Upgrade_power_voidgate_far_desc": "可在兩倍遠的距離開啟裂隙。",
        "Power_strategy_voidgate_far_desc": "開啟裂隙的距離加倍。",
        "Upgrade_power_voidgate_through_name": "穿越虛空",
        "Upgrade_power_voidgate_through_desc": "人、屍體和投擲出的物品都能穿過裂隙。",
        "Power_strategy_voidgate_through_desc":
            "<p>虛空接納的不只是你。守衛、屍體、瓶罐和手榴彈都能穿過裂隙，並保持原本的衝力。</p><br><p>在懸崖上方開啟"
            "出口，讓追兵跟著你過去吧。</p>",
        "Upgrade_power_voidgate_momentum_name": "交錯火力",
        "Upgrade_power_voidgate_momentum_desc": "十字弓箭、子彈和爆破颶風都能穿過裂隙。",
        "Power_strategy_voidgate_momentum_desc":
            "<p>朝一道裂隙射擊，就能命中站在另一道裂隙前的人。</p><br><p>朝裂隙施展的爆破颶風會從另一道裂隙噴湧而出。"
            "</p>",
        "ui_statusinteraction_closevoidgate": "關閉虛空之門",
    },
    "french_m": {
        "Power_voidgate_name": "Porte du Grand Vide",
        "Power_voidgate_desc": "Ouvrez deux failles liées vers le Grand Vide. Entrez dans l’une pour ressortir par "
                               "l’autre.",
        "Power_strategy_voidgate_desc":
            "<p>Chaque utilisation ouvre une faille sur la surface que vous regardez : d’abord l’entrée, puis la "
            "sortie. Une nouvelle faille remplace la plus ancienne. Appuyez brièvement sur la touche d’interaction "
            "pour refermer les deux.</p><br><p>Vous conservez votre vitesse et votre direction en traversant une "
            "faille : tombez dans une faille au sol pour jaillir d’une faille murale. Si une faille vous laisse "
            "loin en contrebas de sa sortie, la touche d’interaction vous ramène aussi à votre point d’entrée.</p>",
        "Upgrade_power_voidgate_far_name": "Faille lointaine",
        "Upgrade_power_voidgate_far_desc": "Les failles peuvent être ouvertes deux fois plus loin.",
        "Power_strategy_voidgate_far_desc": "Double la distance à laquelle vous pouvez ouvrir une faille.",
        "Upgrade_power_voidgate_through_name": "À travers le Grand Vide",
        "Upgrade_power_voidgate_through_desc": "Les personnes, les corps et les objets lancés traversent les "
                                               "failles.",
        "Power_strategy_voidgate_through_desc":
            "<p>Le Grand Vide n’accueille pas que vous. Gardes, corps, bouteilles et grenades traversent les failles "
            "en conservant leur élan.</p><br><p>Ouvrez une sortie au-dessus d’un précipice et laissez vos "
            "poursuivants vous suivre.</p>",
        "Upgrade_power_voidgate_momentum_name": "Feux croisés",
        "Upgrade_power_voidgate_momentum_desc": "Les carreaux, les balles et Rafale traversent les failles.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Tirez dans une faille pour toucher quiconque se tient devant l’autre.</p><br><p>Une Rafale lancée "
            "dans une faille jaillit de sa jumelle.</p>",
        "ui_statusinteraction_closevoidgate": "Fermer les portes",
    },
    "german_m": {
        "Power_voidgate_name": "Tor ins Nichts",
        "Power_voidgate_desc": "Öffne zwei verbundene Risse ins Nichts. Betritt den einen, um aus dem anderen "
                               "herauszukommen.",
        "Power_strategy_voidgate_desc":
            "<p>Jeder Einsatz öffnet einen Riss auf der Oberfläche, auf die du blickst: zuerst den Eingang, dann den "
            "Ausgang. Ein neuer Riss ersetzt den ältesten. Tippe auf die Interaktionstaste, um beide zu schließen."
            "</p><br><p>Durch einen Riss behältst du Geschwindigkeit und Richtung bei: Fall in einen Riss im Boden, "
            "um aus einem Riss in der Wand zu fliegen. Lässt dich ein Riss weit unter seinem Ausgang zurück, bringt "
            "dich die Interaktionstaste auch dorthin zurück, wo du hineingegangen bist.</p>",
        "Upgrade_power_voidgate_far_name": "Ferner Riss",
        "Upgrade_power_voidgate_far_desc": "Risse können doppelt so weit entfernt geöffnet werden.",
        "Power_strategy_voidgate_far_desc": "Verdoppelt die Entfernung, in der du einen Riss öffnen kannst.",
        "Upgrade_power_voidgate_through_name": "Durch das Nichts",
        "Upgrade_power_voidgate_through_desc": "Menschen, Körper und geworfene Gegenstände gelangen durch die "
                                               "Risse.",
        "Power_strategy_voidgate_through_desc":
            "<p>Das Nichts nimmt mehr auf als nur dich. Wachen, Körper, Flaschen und Granaten gelangen durch die "
            "Risse und behalten ihren Schwung.</p><br><p>Öffne einen Ausgang über einem Abgrund und lass deine "
            "Verfolger dir folgen.</p>",
        "Upgrade_power_voidgate_momentum_name": "Kreuzfeuer",
        "Upgrade_power_voidgate_momentum_desc": "Bolzen, Kugeln und Windstoß gelangen durch die Risse.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Schieß in einen Riss, um jeden zu treffen, der vor dem anderen steht.</p><br><p>Ein in einen Riss "
            "gewirkter Windstoß bricht aus seinem Zwilling hervor.</p>",
        "ui_statusinteraction_closevoidgate": "Tore schließen",
    },
    "italian_m": {
        "Power_voidgate_name": "Portale dell’Oblio",
        "Power_voidgate_desc": "Apri due varchi collegati nell’Oblio. Entra in uno per uscire dall’altro.",
        "Power_strategy_voidgate_desc":
            "<p>Ogni utilizzo apre un varco sulla superficie che stai guardando: prima l’ingresso, poi l’uscita. Un "
            "nuovo varco sostituisce il più vecchio. Premi brevemente il tasto di interazione per chiuderli "
            "entrambi.</p><br><p>Attraverso un varco mantieni velocità e direzione: cadi in un varco sul pavimento "
            "per volare fuori da un varco su una parete. Se un varco ti lascia molto più in basso della sua uscita, "
            "il tasto di interazione ti riporta anche al punto d’ingresso.</p>",
        "Upgrade_power_voidgate_far_name": "Varco Lontano",
        "Upgrade_power_voidgate_far_desc": "I varchi possono essere aperti al doppio della distanza.",
        "Power_strategy_voidgate_far_desc": "Raddoppia la distanza a cui puoi aprire un varco.",
        "Upgrade_power_voidgate_through_name": "Attraverso l’Oblio",
        "Upgrade_power_voidgate_through_desc": "Persone, corpi e oggetti lanciati passano attraverso i varchi.",
        "Power_strategy_voidgate_through_desc":
            "<p>L’Oblio non accoglie solo te. Guardie, corpi, bottiglie e granate passano attraverso i varchi e "
            "mantengono il loro slancio.</p><br><p>Apri un’uscita sopra un precipizio e lascia che i tuoi "
            "inseguitori ti seguano.</p>",
        "Upgrade_power_voidgate_momentum_name": "Fuoco Incrociato",
        "Upgrade_power_voidgate_momentum_desc": "Dardi, proiettili e Ciclone passano attraverso i varchi.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Spara in un varco per colpire chiunque si trovi davanti all’altro.</p><br><p>Un Ciclone lanciato in "
            "un varco erompe dal suo gemello.</p>",
        "ui_statusinteraction_closevoidgate": "Chiudi i portali",
    },
    "japanese_m": {
        "Power_voidgate_name": "ヴォイドゲート",
        "Power_voidgate_desc": "虚無へと続く2つの裂け目を開く。片方に入ると、もう片方から出てくる",
        "Power_strategy_voidgate_desc":
            "<p>使用するたびに、見ている面に裂け目が開く。最初が入口、次が出口になる。新しい裂け目は最も古い裂け目と"
            "置き換わる。インタラクトキーを短く押すと両方とも閉じる</p><br><p>裂け目を通り抜けても速度と向きは保たれる。"
            "床の裂け目に落ちれば、壁の裂け目から飛び出すことができる。裂け目を抜けて出口のはるか下に取り残された時も、"
            "インタラクトキーで入った場所に戻れる</p>",
        "Upgrade_power_voidgate_far_name": "ファーリフト",
        "Upgrade_power_voidgate_far_desc": "2倍の距離まで裂け目を開けるようになる",
        "Power_strategy_voidgate_far_desc": "裂け目を開ける距離が2倍になる",
        "Upgrade_power_voidgate_through_name": "ヴォイドパッセージ",
        "Upgrade_power_voidgate_through_desc": "人間、死体、投げた物体も裂け目を通り抜ける",
        "Power_strategy_voidgate_through_desc":
            "<p>虚無が受け入れるのは自分だけではない。衛兵、死体、瓶、グレネードも勢いを保ったまま裂け目を通り抜ける"
            "</p><br><p>断崖の上に出口を開いて、追っ手を誘い込もう</p>",
        "Upgrade_power_voidgate_momentum_name": "クロスファイア",
        "Upgrade_power_voidgate_momentum_desc": "ボルト、弾丸、ウィンドブラストが裂け目を通り抜ける",
        "Power_strategy_voidgate_momentum_desc":
            "<p>片方の裂け目に撃ち込めば、もう片方の前に立つ者に命中する</p><br><p>裂け目に向けて放ったウィンドブラストは、"
            "もう片方の裂け目から吹き出す</p>",
        "ui_statusinteraction_closevoidgate": "ゲートを閉じる",
    },
    "mexican_m": {
        "Power_voidgate_name": "Puerta del Vacío",
        "Power_voidgate_desc": "Abre dos grietas enlazadas hacia el Vacío. Entra en una para salir por la otra.",
        "Power_strategy_voidgate_desc":
            "<p>Cada uso abre una grieta en la superficie que miras: primero la entrada y luego la salida. Una nueva "
            "grieta reemplaza a la más antigua. Presiona brevemente la tecla de interacción para cerrar ambas.</p>"
            "<br><p>Al cruzar una grieta conservas tu velocidad y dirección: cae en una grieta del suelo para salir "
            "volando de una grieta en la pared. Si una grieta te deja muy por debajo de su salida, la tecla de "
            "interacción también te regresa al lugar por donde entraste.</p>",
        "Upgrade_power_voidgate_far_name": "Grieta lejana",
        "Upgrade_power_voidgate_far_desc": "Las grietas pueden abrirse al doble de distancia.",
        "Power_strategy_voidgate_far_desc": "Duplica la distancia a la que puedes abrir una grieta.",
        "Upgrade_power_voidgate_through_name": "A través del Vacío",
        "Upgrade_power_voidgate_through_desc": "Las personas, los cuerpos y los objetos lanzados atraviesan las "
                                               "grietas.",
        "Power_strategy_voidgate_through_desc":
            "<p>El Vacío no solo te acepta a ti. Guardias, cuerpos, botellas y granadas atraviesan las grietas y "
            "conservan su impulso.</p><br><p>Abre una salida sobre un precipicio y deja que tus perseguidores te "
            "sigan.</p>",
        "Upgrade_power_voidgate_momentum_name": "Fuego cruzado",
        "Upgrade_power_voidgate_momentum_desc": "Las flechas, las balas y Ráfaga atraviesan las grietas.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Dispara a una grieta para alcanzar a quien esté frente a la otra.</p><br><p>Una Ráfaga lanzada hacia "
            "una grieta sale con fuerza por su gemela.</p>",
        "ui_statusinteraction_closevoidgate": "Cerrar las puertas",
    },
    "polish_m": {
        "Power_voidgate_name": "Wrota Pustki",
        "Power_voidgate_desc": "Otwierasz dwie połączone szczeliny do Pustki. Wejdź w jedną, by wyjść z drugiej.",
        "Power_strategy_voidgate_desc":
            "<p>Każde użycie otwiera szczelinę na powierzchni, na którą patrzysz: najpierw wejście, potem wyjście. "
            "Nowa szczelina zastępuje najstarszą. Krótko naciśnij klawisz interakcji, by zamknąć obie.</p><br><p>"
            "Przechodząc przez szczelinę, zachowujesz prędkość i kierunek: wpadnij do szczeliny w podłodze, by "
            "wylecieć ze szczeliny w ścianie. Jeśli szczelina pozostawi cię daleko poniżej wyjścia, klawisz "
            "interakcji przeniesie cię też z powrotem do miejsca wejścia.</p>",
        "Upgrade_power_voidgate_far_name": "Odległa szczelina",
        "Upgrade_power_voidgate_far_desc": "Szczeliny można otwierać dwa razy dalej.",
        "Power_strategy_voidgate_far_desc": "Podwaja odległość, z jakiej możesz otworzyć szczelinę.",
        "Upgrade_power_voidgate_through_name": "Przez Pustkę",
        "Upgrade_power_voidgate_through_desc": "Ludzie, ciała i rzucone przedmioty przechodzą przez szczeliny.",
        "Power_strategy_voidgate_through_desc":
            "<p>Pustka przyjmuje nie tylko ciebie. Strażnicy, ciała, butelki i granaty przechodzą przez szczeliny, "
            "zachowując swój pęd.</p><br><p>Otwórz wyjście nad przepaścią i pozwól, by ścigający podążyli za tobą."
            "</p>",
        "Upgrade_power_voidgate_momentum_name": "Krzyżowy ogień",
        "Upgrade_power_voidgate_momentum_desc": "Bełty, kule i podmuch przechodzą przez szczeliny.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Strzel w jedną szczelinę, by trafić każdego, kto stoi przed drugą.</p><br><p>Podmuch rzucony w "
            "szczelinę wydobywa się z jej bliźniaczki.</p>",
        "ui_statusinteraction_closevoidgate": "Zamknij wrota",
    },
    "spanish_m": {
        "Power_voidgate_name": "Puerta del Vacío",
        "Power_voidgate_desc": "Abre dos grietas enlazadas hacia el Vacío. Entra en una para salir por la otra.",
        "Power_strategy_voidgate_desc":
            "<p>Cada uso abre una grieta en la superficie que miras: primero la entrada y luego la salida. Una nueva "
            "grieta sustituye a la más antigua. Pulsa brevemente la tecla de interacción para cerrar ambas.</p><br>"
            "<p>Al cruzar una grieta conservas tu velocidad y dirección: cae en una grieta del suelo para salir "
            "volando por una grieta de la pared. Si una grieta te deja muy por debajo de su salida, la tecla de "
            "interacción también te devuelve al lugar por el que entraste.</p>",
        "Upgrade_power_voidgate_far_name": "Grieta lejana",
        "Upgrade_power_voidgate_far_desc": "Las grietas pueden abrirse al doble de distancia.",
        "Power_strategy_voidgate_far_desc": "Duplica la distancia a la que puedes abrir una grieta.",
        "Upgrade_power_voidgate_through_name": "A través del Vacío",
        "Upgrade_power_voidgate_through_desc": "Las personas, los cuerpos y los objetos lanzados atraviesan las "
                                               "grietas.",
        "Power_strategy_voidgate_through_desc":
            "<p>El Vacío no solo te acepta a ti. Guardas, cuerpos, botellas y granadas atraviesan las grietas y "
            "conservan su impulso.</p><br><p>Abre una salida sobre un precipicio y deja que tus perseguidores te "
            "sigan.</p>",
        "Upgrade_power_voidgate_momentum_name": "Fuego cruzado",
        "Upgrade_power_voidgate_momentum_desc": "Los virotes, las balas y Ráfaga atraviesan las grietas.",
        "Power_strategy_voidgate_momentum_desc":
            "<p>Dispara a una grieta para alcanzar a quien esté delante de la otra.</p><br><p>Una Ráfaga lanzada "
            "hacia una grieta sale con fuerza por su gemela.</p>",
        "ui_statusinteraction_closevoidgate": "Cerrar las puertas",
    },
}
# Far Reach's "Cancel Far Reach" prompt also shows while aiming the Void Gate: shortened to "Cancel" in every language
SHORT_CANCEL = {"english_m": "Cancel", "russian_m": "Отменить", "brazilian_m": "Cancelar", "chinese_m": "取消",
                "french_m": "Annuler", "german_m": "Abbrechen", "italian_m": "Annulla", "japanese_m": "キャンセル",
                "mexican_m": "Cancelar", "polish_m": "Anuluj", "spanish_m": "Cancelar"}


def write(lang, texts):
    for k, v in texts.items():
        assert '"' not in v, f"{lang} {k}: ASCII double quote"
    p = os.path.join(MOD, lang + ".json")
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        json.dump(texts, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("wrote", os.path.relpath(p, os.path.join(MOD, "..")), len(texts), "texts")


def main():
    texts = dict(OTHER, english_m=EN, russian_m=RU)
    for lang, t in sorted(texts.items()):
        assert set(t) == set(EN), (lang, set(t) ^ set(EN))
        write(lang, dict(t, ui_statusinteraction_cancelfarreach=SHORT_CANCEL[lang]))


if __name__ == "__main__":
    main()
