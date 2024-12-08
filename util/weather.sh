#!/bin/sh

bin/weather.perl
bin/post Record 全省今日各地天氣預報 [氣象小姐] etc/weather.today
bin/post Record 全省明日各地天氣預報 [氣象小姐] etc/weather.tomorrow
