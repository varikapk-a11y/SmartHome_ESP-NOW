<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Расширенный тест звука для Android 4.4</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            padding: 20px; 
            background: #f5f5f5;
            max-width: 800px;
            margin: 0 auto;
        }
        .container { 
            background: white; 
            border-radius: 10px; 
            padding: 20px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1, h2 { color: #333; }
        .test-section { 
            margin: 20px 0; 
            padding: 15px; 
            border: 1px solid #ddd; 
            border-radius: 5px;
        }
        .test-button { 
            margin: 5px; 
            padding: 10px 15px; 
            background: #4CAF50; 
            color: white; 
            border: none; 
            border-radius: 4px; 
            cursor: pointer;
            font-size: 16px;
            transition: background 0.3s;
        }
        .test-button:hover { background: #45a049; }
        .test-button.danger { background: #f44336; }
        .test-button.danger:hover { background: #d32f2f; }
        .test-button.warning { background: #ff9800; }
        .test-button.warning:hover { background: #f57c00; }
        .status { 
            margin-top: 10px; 
            padding: 10px; 
            border-radius: 4px; 
            display: none;
        }
        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
        .info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
        .results { 
            margin-top: 20px; 
            padding: 15px; 
            background: #e8f4fd; 
            border-radius: 5px;
        }
        .sound-info { color: #666; font-size: 14px; }
        .method-label { 
            display: inline-block; 
            width: 250px; 
            font-weight: bold; 
            margin-right: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔊 Расширенный тест звука для Android 4.4+</h1>
        <p class="sound-info">
            <strong>Цель:</strong> Протестировать все возможные способы воспроизведения звука на старых Android устройствах.<br>
            <strong>Инструкция:</strong> Нажимайте кнопки по порядку и записывайте результаты.
        </p>
        
        <div class="test-section">
            <h2>1. Базовый Audio API (стандартные методы)</h2>
            
            <div>
                <span class="method-label">Audio() конструктор + play():</span>
                <button class="test-button" onclick="testMethod1()">Тест 1</button>
                <div id="status1" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">createElement('audio') + play():</span>
                <button class="test-button" onclick="testMethod2()">Тест 2</button>
                <div id="status2" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">createElement('audio') с автоигрой:</span>
                <button class="test-button" onclick="testMethod3()">Тест 3</button>
                <div id="status3" class="status"></div>
            </div>
        </div>
        
        <div class="test-section">
            <h2>2. Web Audio API (современные методы)</h2>
            
            <div>
                <span class="method-label">Oscillator (синусоида):</span>
                <button class="test-button" onclick="testMethod4()">Тест 4</button>
                <div id="status4" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">Buffer с коротким звуком:</span>
                <button class="test-button" onclick="testMethod5()">Тест 5</button>
                <div id="status5" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">Oscillator с обработкой событий:</span>
                <button class="test-button warning" onclick="testMethod6()">Тест 6</button>
                <div id="status6" class="status"></div>
            </div>
        </div>
        
        <div class="test-section">
            <h2>3. Старые/альтернативные методы</h2>
            
            <div>
                <span class="method-label">HTML5 Audio с canplaythrough:</span>
                <button class="test-button" onclick="testMethod7()">Тест 7</button>
                <div id="status7" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">Audio с user gesture обёрткой:</span>
                <button class="test-button" onclick="testMethod8()">Тест 8</button>
                <div id="status8" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">embed элемент (устаревший):</span>
                <button class="test-button" onclick="testMethod9()">Тест 9</button>
                <div id="status9" class="status"></div>
            </div>
        </div>
        
        <div class="test-section">
            <h2>4. Экспериментальные методы</h2>
            
            <div>
                <span class="method-label">Audio в iframe:</span>
                <button class="test-button warning" onclick="testMethod10()">Тест 10</button>
                <div id="status10" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">Несколько Audio объектов:</span>
                <button class="test-button warning" onclick="testMethod11()">Тест 11</button>
                <div id="status11" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">AudioContext.resume() в обработчике:</span>
                <button class="test-button danger" onclick="testMethod12()">Тест 12</button>
                <div id="status12" class="status"></div>
            </div>
        </div>
        
        <div class="test-section">
            <h2>5. Тревога (сирена) тесты</h2>
            
            <div>
                <span class="method-label">Периодический звук (1 сек):</span>
                <button class="test-button" onclick="testAlarm1()">Тест сирены A</button>
                <div id="alarmStatus1" class="status"></div>
            </div>
            
            <div>
                <span class="method-label">Быстрая сирена (частые тоны):</span>
                <button class="test-button warning" onclick="testAlarm2()">Тест сирены B</button>
                <button class="test-button danger" onclick="stopAlarms()">Остановить все</button>
                <div id="alarmStatus2" class="status"></div>
            </div>
        </div>
        
        <div class="results">
            <h2>📋 Результаты тестирования</h2>
            <p id="resultsText">Нажмите кнопки выше, чтобы начать тестирование...</p>
            <button class="test-button" onclick="saveResults()">Сохранить результаты</button>
            <button class="test-button danger" onclick="clearResults()">Очистить</button>
        </div>
    </div>

    <script>
        let testResults = [];
        let activeAlarms = [];
        
        // Базовый Audio API методы
        function testMethod1() {
            try {
                let audio = new Audio();
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                audio.play();
                showStatus('status1', '✅ Воспроизведение запущено', 'success');
                testResults.push('Метод 1 (Audio конструктор): УСПЕХ');
            } catch (e) {
                showStatus('status1', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 1 (Audio конструктор): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod2() {
            try {
                let audio = document.createElement('audio');
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                audio.play();
                showStatus('status2', '✅ Воспроизведение запущено', 'success');
                testResults.push('Метод 2 (createElement audio): УСПЕХ');
            } catch (e) {
                showStatus('status2', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 2 (createElement audio): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod3() {
            try {
                let audio = document.createElement('audio');
                audio.autoplay = true;
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                showStatus('status3', '✅ Автовоспроизведение установлено', 'info');
                testResults.push('Метод 3 (autoplay audio): НАСТРОЙКА ВЫПОЛНЕНА');
            } catch (e) {
                showStatus('status3', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 3 (autoplay audio): ОШИБКА - ' + e.message);
            }
        }
        
        // Web Audio API методы
        function testMethod4() {
            try {
                let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                let oscillator = audioContext.createOscillator();
                oscillator.connect(audioContext.destination);
                oscillator.frequency.value = 800;
                oscillator.start();
                setTimeout(() => oscillator.stop(), 500);
                showStatus('status4', '✅ Синусоидальный тон воспроизведён (500ms)', 'success');
                testResults.push('Метод 4 (Web Audio Oscillator): УСПЕХ');
            } catch (e) {
                showStatus('status4', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 4 (Web Audio Oscillator): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod5() {
            try {
                let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                let buffer = audioContext.createBuffer(1, 22050, 22050);
                let data = buffer.getChannelData(0);
                for (let i = 0; i < 22050; i++) {
                    data[i] = Math.sin(i * 0.1) * 0.1;
                }
                let source = audioContext.createBufferSource();
                source.buffer = buffer;
                source.connect(audioContext.destination);
                source.start();
                showStatus('status5', '✅ Буферный звук воспроизведён', 'success');
                testResults.push('Метод 5 (Web Audio Buffer): УСПЕХ');
            } catch (e) {
                showStatus('status5', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 5 (Web Audio Buffer): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod6() {
            try {
                let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                audioContext.resume().then(() => {
                    let oscillator = audioContext.createOscillator();
                    oscillator.connect(audioContext.destination);
                    oscillator.frequency.value = 600;
                    oscillator.start();
                    setTimeout(() => oscillator.stop(), 300);
                    showStatus('status6', '✅ Воспроизведение после resume()', 'success');
                    testResults.push('Метод 6 (AudioContext resume): УСПЕХ');
                }).catch(e => {
                    showStatus('status6', '❌ Ошибка resume: ' + e.message, 'error');
                    testResults.push('Метод 6 (AudioContext resume): ОШИБКА - ' + e.message);
                });
            } catch (e) {
                showStatus('status6', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 6 (AudioContext resume): ОШИБКА - ' + e.message);
            }
        }
        
        // Старые методы
        function testMethod7() {
            try {
                let audio = new Audio();
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                audio.addEventListener('canplaythrough', () => {
                    audio.play();
                    showStatus('status7', '✅ Воспроизведение после canplaythrough', 'success');
                    testResults.push('Метод 7 (canplaythrough): УСПЕХ');
                });
                audio.load();
            } catch (e) {
                showStatus('status7', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 7 (canplaythrough): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod8() {
            try {
                // Попытка обойти ограничения пользовательского жеста
                let audio = new Audio();
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                audio.volume = 1.0;
                
                // Несколько попыток запуска
                let playPromise = audio.play();
                if (playPromise !== undefined) {
                    playPromise.then(() => {
                        showStatus('status8', '✅ Promise воспроизведения выполнен', 'success');
                        testResults.push('Метод 8 (Promise play): УСПЕХ');
                    }).catch(e => {
                        // Пробуем еще раз
                        setTimeout(() => audio.play(), 100);
                        showStatus('status8', '⚠️ Первая попытка отклонена, вторая запущена', 'info');
                        testResults.push('Метод 8 (Promise play): ПОВТОРНАЯ ПОПЫТКА');
                    });
                }
            } catch (e) {
                showStatus('status8', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 8 (Promise play): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod9() {
            try {
                // Устаревший метод embed для старых браузеров
                let embed = document.createElement('embed');
                embed.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                embed.type = 'audio/wav';
                embed.style.display = 'none';
                document.body.appendChild(embed);
                showStatus('status9', '✅ Embed элемент создан (устаревший метод)', 'info');
                testResults.push('Метод 9 (embed элемент): СОЗДАН');
            } catch (e) {
                showStatus('status9', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 9 (embed элемент): ОШИБКА - ' + e.message);
            }
        }
        
        // Экспериментальные методы
        function testMethod10() {
            try {
                // Аудио внутри iframe может обойти ограничения
                let iframe = document.createElement('iframe');
                iframe.style.display = 'none';
                document.body.appendChild(iframe);
                
                let audio = iframe.contentDocument.createElement('audio');
                audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                audio.play();
                
                showStatus('status10', '✅ Audio в iframe создано', 'info');
                testResults.push('Метод 10 (iframe audio): СОЗДАН');
            } catch (e) {
                showStatus('status10', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 10 (iframe audio): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod11() {
            try {
                // Создание нескольких аудио объектов
                for (let i = 0; i < 3; i++) {
                    let audio = new Audio();
                    audio.src = 'data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAZGF0YQQAAAAAAA==';
                    audio.play().catch(() => { /* игнорируем ошибки */ });
                }
                showStatus('status11', '✅ 3 Audio объекта созданы', 'info');
                testResults.push('Метод 11 (несколько Audio): СОЗДАНЫ');
            } catch (e) {
                showStatus('status11', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 11 (несколько Audio): ОШИБКА - ' + e.message);
            }
        }
        
        function testMethod12() {
            try {
                // Попытка использовать событие touchstart для активации
                document.addEventListener('touchstart', function activateAudio() {
                    let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                    audioContext.resume().then(() => {
                        let oscillator = audioContext.createOscillator();
                        oscillator.connect(audioContext.destination);
                        oscillator.frequency.value = 1000;
                        oscillator.start();
                        setTimeout(() => oscillator.stop(), 200);
                    });
                    document.removeEventListener('touchstart', activateAudio);
                }, {once: true});
                
                showStatus('status12', '✅ Обработчик touchstart установлен', 'info');
                testResults.push('Метод 12 (touchstart активация): НАСТРОЙКА ВЫПОЛНЕНА');
            } catch (e) {
                showStatus('status12', '❌ Ошибка: ' + e.message, 'error');
                testResults.push('Метод 12 (touchstart активация): ОШИБКА - ' + e.message);
            }
        }
        
        // Тесты тревоги (сирены)
        function testAlarm1() {
            try {
                let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                let oscillator = audioContext.createOscillator();
                oscillator.connect(audioContext.destination);
                oscillator.frequency.value = 900;
                oscillator.start();
                
                activeAlarms.push({oscillator: oscillator, context: audioContext});
                
                setTimeout(() => {
                    oscillator.stop();
                    // Удаляем из активных
                    let index = activeAlarms.findIndex(a => a.oscillator === oscillator);
                    if (index > -1) activeAlarms.splice(index, 1);
                }, 1000);
                
                showStatus('alarmStatus1', '✅ Сирена (1 сек) запущена', 'success');
                testResults.push('Сирена A (1 сек): ЗАПУЩЕНА');
            } catch (e) {
                showStatus('alarmStatus1', '❌ Ошибка сирены: ' + e.message, 'error');
                testResults.push('Сирена A: ОШИБКА - ' + e.message);
            }
        }
        
        function testAlarm2() {
            try {
                let audioContext = new (window.AudioContext || window.webkitAudioContext)();
                
                function playTone(freq, duration) {
                    let oscillator = audioContext.createOscillator();
                    oscillator.connect(audioContext.destination);
                    oscillator.frequency.value = freq;
                    oscillator.start();
                    
                    setTimeout(() => oscillator.stop(), duration);
                }
                
                // Быстрая сирена (чередующиеся тоны)
                let interval = setInterval(() => {
                    playTone(800, 100);
                    setTimeout(() => playTone(1200, 100), 200);
                }, 500);
                
                let alarmObj = {interval: interval, context: audioContext};
                activeAlarms.push(alarmObj);
                
                // Автоостановка через 5 сек
                setTimeout(() => {
                    clearInterval(interval);
                    let index = activeAlarms.findIndex(a => a.interval === interval);
                    if (index > -1) activeAlarms.splice(index, 1);
                }, 5000);
                
                showStatus('alarmStatus2', '✅ Быстрая сирена запущена (5 сек)', 'success');
                testResults.push('Сирена B (быстрая): ЗАПУЩЕНА');
            } catch (e) {
                showStatus('alarmStatus2', '❌ Ошибка сирены: ' + e.message, 'error');
                testResults.push('Сирена B: ОШИБКА - ' + e.message);
            }
        }
        
        function stopAlarms() {
            activeAlarms.forEach(alarm => {
                if (alarm.oscillator) alarm.oscillator.stop();
                if (alarm.interval) clearInterval(alarm.interval);
                if (alarm.context) alarm.context.close();
            });
            activeAlarms = [];
            showStatus('alarmStatus1', '⏹️ Все сирены остановлены', 'info');
            showStatus('alarmStatus2', '⏹️ Все сирены остановлены', 'info');
        }
        
        // Вспомогательные функции
        function showStatus(elementId, message, type) {
            let element = document.getElementById(elementId);
            element.textContent = message;
            element.className = 'status ' + type;
            element.style.display = 'block';
            
            // Обновляем результаты
            updateResults();
        }
        
        function updateResults() {
            let resultsText = document.getElementById('resultsText');
            resultsText.innerHTML = '<strong>Тестов выполнено:</strong> ' + testResults.length + '<br><br>';
            
            testResults.forEach((result, index) => {
                let color = result.includes('УСПЕХ') ? 'green' : 
                           result.includes('ОШИБКА') ? 'red' : 'orange';
                resultsText.innerHTML += `<span style="color: ${color}">${index + 1}. ${result}</span><br>`;
            });
        }
        
        function saveResults() {
            let resultsString = "=== РЕЗУЛЬТАТЫ ТЕСТА ЗВУКА ===\nДата: " + new Date().toLocaleString() + "\n\n";
            resultsString += testResults.join('\n');
            
            // Создаем blob и ссылку для скачивания
            let blob = new Blob([resultsString], {type: 'text/plain'});
            let url = URL.createObjectURL(blob);
            let a = document.createElement('a');
            a.href = url;
            a.download = 'sound-test-results-' + Date.now() + '.txt';
            a.click();
            
            showStatus('resultsText', '✅ Результаты сохранены в файл', 'success');
        }
        
        function clearResults() {
            testResults = [];
            updateResults();
            let statusElements = document.querySelectorAll('.status');
            statusElements.forEach(el => {
                el.style.display = 'none';
            });
        }
        
        // Инициализация
        document.addEventListener('DOMContentLoaded', () => {
            updateResults();
            console.log('Тест звука инициализирован. Проверяем поддержку Web Audio API:', 
                       !!(window.AudioContext || window.webkitAudioContext));
        });
    </script>
</body>
</html>